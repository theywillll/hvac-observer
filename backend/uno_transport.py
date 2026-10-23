"""Bounded UNO snapshot receipt checks. No claim of hardware timing qualification."""
from copy import deepcopy
import json
from .validation import validate
from .current_quality import CURRENT_FIELDS

# Conservative application text budget, NOT a qualified router/serial limit.
MAX_SNAPSHOT_BYTES = 896
MAX_CLOCK_NS = (1 << 64) - 1


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate MCU JSON key: ' + key)
        result[key] = value
    return result


def _constant(value):
    raise ValueError('Nonfinite MCU JSON constant: ' + value)


class SnapshotReceiver:
    """One stream owner; reconstruct after a reset/error, never splice sessions.

    Source age is retained unchanged. A numeric result must separately satisfy
    a conservative host receipt deadline. That is not a lease for later display.
    """
    def __init__(self):
        self._frame_seq = None
        self._received_ns = None
        self._session = None
        self._age_policy = None
        self._calibrations = [None, None, None]
        self._windows = [None, None, None]

    def receive(self, raw, started_ns, received_ns, *, timestamp):
        for value in (started_ns, received_ns):
            if type(value) is not int or not 0 <= value <= MAX_CLOCK_NS:
                raise ValueError('Invalid host monotonic request clock')
        if received_ns < started_ns or (self._received_ns is not None and started_ns < self._received_ns):
            raise ValueError('Backward/overlapping MCU request clock')
        if type(raw) is not str or len(raw) > MAX_SNAPSHOT_BYTES or not raw.isascii():
            raise ValueError('MCU snapshot must be bounded ASCII JSON text')
        try:
            frame = json.loads(raw, object_pairs_hook=_object, parse_constant=_constant)
        except (json.JSONDecodeError, RecursionError) as exc:
            raise ValueError('Invalid MCU snapshot JSON') from exc
        if type(frame) is not dict or 'ts' in frame or 'source' in frame:
            raise ValueError('MCU snapshot must be an object without host-owned timestamp/source')
        seq = frame.get('seq')
        if type(seq) is not int or not 0 <= seq <= (1 << 32)-1:
            raise ValueError('MCU frame sequence must be uint32')
        if self._frame_seq is not None and seq <= self._frame_seq:
            raise ValueError('MCU frame stalled/reset; restart acquisition')
        frame.update(ts=timestamp, source='hardware')
        checked, _ = validate(frame)
        quality = checked.get('current_quality')
        session = quality['session'] if quality is not None else None
        if self._session is not None and session != self._session:
            raise ValueError('Current session changed/disappeared; restart acquisition')
        if self._age_policy is not None and quality['max_age_us'] != self._age_policy:
            raise ValueError('Current age policy changed within session')
        # Work on a fixed three-channel copy; failures never partially consume
        # frame/window ordinals or tighten another channel's receipt deadline.
        windows = deepcopy(self._windows)
        calibrations = list(self._calibrations)
        if quality is not None:
            for c, item in enumerate(quality['channels']):
                ordinal = item['window_seq']
                previous = windows[c]
                if ordinal is None:
                    if previous is not None:
                        raise ValueError('Observed current window disappeared')
                    continue
                ordinal = int(ordinal)
                same = previous is not None and ordinal == previous['seq']
                if previous is not None and ordinal < previous['seq']:
                    raise ValueError('Current window moved backward')
                if item['available']:
                    calibration = (item['calibration_id'], item['calibration_revision'])
                    if calibrations[c] is not None and calibration != calibrations[c]:
                        raise ValueError('Current calibration changed within session')
                    calibrations[c] = calibration
                    fingerprint = (checked[CURRENT_FIELDS[c]], item['timing_evidence_id'],
                                   item['calibration_id'], item['calibration_revision'])
                    # The source must create this fresh snapshot DURING the
                    # request. Starting the lease at host request start is
                    # conservative; queued/cached pre-request JSON cannot qualify.
                    deadline = started_ns + (quality['max_age_us']-item['age_us'])*1000
                    if same:
                        if previous['revoked'] or fingerprint != previous['fingerprint']:
                            raise ValueError('Current window was revived or changed')
                        if item['age_us'] < previous['age_us']:
                            raise ValueError('Current window age moved backward')
                        deadline = min(deadline, previous['deadline'])
                    if received_ns > deadline:
                        raise ValueError('Current window expired before host receipt')
                    windows[c] = dict(seq=ordinal, revoked=False, fingerprint=fingerprint,
                                      age_us=item['age_us'], deadline=deadline)
                else:
                    # A failed/expired ordinal cannot become numeric later.
                    windows[c] = dict(seq=ordinal, revoked=True, fingerprint=None,
                                      age_us=None, deadline=None)
        self._windows = windows
        self._calibrations = calibrations
        self._session = session if session is not None else self._session
        if session is not None:
            self._age_policy = quality['max_age_us']
        self._frame_seq = seq
        self._received_ns = received_ns
        return checked
