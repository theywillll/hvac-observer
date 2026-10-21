"""Schema 1 checks wire consistency, not the truth of bench qualification claims."""
from copy import deepcopy

CURRENT_FIELDS = ('compressor_a', 'blower_a', 'condenser_fan_a')
UINT32_MAX = (1 << 32) - 1
FAULT_MASK = (1 << 14) - 1
CONFIG, AGE, PENDING = 1, 128, 2048


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _decimal64(value, allow_zero=False):
    return (type(value) is str and 1 <= len(value) <= 20 and value.isascii()
            and value.isdecimal() and (value == '0' or value[0] != '0')
            and (allow_zero or value != '0') and int(value) <= (1 << 64) - 1)


def validate_current_quality(frame):
    """Return an independent copy or reject. Legacy all-null hardware is allowed."""
    if 'current_quality' not in frame:
        if frame.get('source') == 'hardware' and any(frame.get(k) is not None for k in CURRENT_FIELDS):
            raise ValueError('Hardware current requires current_quality')
        return None
    q = frame['current_quality']
    if type(q) is not dict or set(q) != {'schema', 'session', 'max_age_us', 'channels'}:
        raise ValueError('Invalid current_quality fields')
    if type(q['schema']) is not int or q['schema'] != 1:
        raise ValueError('Unsupported current_quality schema')
    session = q['session']
    if session is not None and not _decimal64(session):
        raise ValueError('Current session must be a nonzero uint64 decimal string or null')
    limit = q['max_age_us']
    if not _integer(limit, 0, (1 << 31) - 1) or ((session is None) != (limit == 0)):
        raise ValueError('Invalid current age policy/session')
    channels = q['channels']
    if type(channels) is not list or len(channels) != 3:
        raise ValueError('Three ordered current channels required')
    keys = {'channel', 'available', 'faults', 'age_us', 'window_seq',
            'timing_evidence_id', 'calibration_id', 'calibration_revision'}
    for c, item in enumerate(channels):
        if type(item) is not dict or set(item) != keys:
            raise ValueError('Invalid current channel fields')
        if type(item['channel']) is not int or item['channel'] != c or type(item['available']) is not bool:
            raise ValueError('Invalid current channel order/availability')
        faults, age, seq = item['faults'], item['age_us'], item['window_seq']
        if not _integer(faults, 0, FAULT_MASK) or (age is not None and not _integer(age, 0, UINT32_MAX-1)):
            raise ValueError('Invalid current fault mask/age')
        ids = [item[k] for k in ('timing_evidence_id', 'calibration_id', 'calibration_revision')]
        if any(v is not None and not _integer(v, 1, UINT32_MAX) for v in ids):
            raise ValueError('Invalid current evidence/calibration identifier')
        if (item['calibration_id'] is None) != (item['calibration_revision'] is None):
            raise ValueError('Calibration identifier/revision must be paired')
        if seq is None:
            if age is not None or any(v is not None for v in ids) or not faults & (CONFIG | PENDING):
                raise ValueError('Unobserved current channel has window metadata')
        elif not _decimal64(seq, allow_zero=True) or session is None or faults & (CONFIG | PENDING):
            raise ValueError('Invalid current window identity')
        if session is None and (not faults & CONFIG or seq is not None):
            raise ValueError('Unconfigured current channel requires config fault')
        if item['available']:
            if (faults != 0 or frame.get(CURRENT_FIELDS[c]) is None or seq is None
                    or age is None or age > limit or any(v is None for v in ids)):
                raise ValueError('Available current lacks fresh, qualified metadata/value')
        elif faults == 0 or frame.get(CURRENT_FIELDS[c]) is not None:
            raise ValueError('Unavailable current must have faults and a null value')
        if seq is not None and age is not None and age > limit and not faults & AGE:
            raise ValueError('Expired current lacks age fault')
    return deepcopy(q)
