"""Strict wire contract; missing is never converted to zero."""
import math
from datetime import datetime, timezone

RANGES = {
    'return_c': (-40, 100), 'supply_c': (-40, 120), 'outdoor_c': (-60, 70),
    'indoor_rh': (0, 100), 'supply_rh': (0, 100), 'filter_pa': (-500, 500),
    'static_pa': (-1500, 1500), 'compressor_a': (0, 200), 'blower_a': (0, 100),
    'condenser_fan_a': (0, 30), 'power_w': (0, 50000), 'voltage_v': (0, 300),
    'vibration_g': (0, 16), 'blower_vibration_g': (0, 16), 'suction_c': (-60, 150),
    'liquid_c': (-60, 150), 'outdoor_unit_c': (-60, 150), 'airflow_proxy': (0, 5),
}
SIGNALS = ('R', 'Y', 'Y2', 'W', 'W2', 'G', 'OB', 'condensate', 'defrost')

def validate(frame):
    if not isinstance(frame, dict):
        raise ValueError('Frame must be an object')
    allowed = set(RANGES) | set(SIGNALS) | {'ts', 'seq', 'source', 'stage'}
    if set(frame) - allowed:
        raise ValueError('Unknown fields: ' + ', '.join(sorted(set(frame)-allowed)))
    if not isinstance(frame.get('ts'), str):
        raise ValueError('ts must be an ISO 8601 string')
    try:
        ts = datetime.fromisoformat(frame['ts'].replace('Z', '+00:00'))
    except ValueError as exc:
        raise ValueError('Invalid timestamp') from exc
    if ts.utcoffset() is None:
        raise ValueError('Timezone required')
    seq = frame.get('seq')
    if type(seq) is not int or seq < 0:
        raise ValueError('seq must be a nonnegative integer')
    if frame.get('source') not in ('simulator', 'hardware', 'replay'):
        raise ValueError('source required: simulator, hardware or replay')
    result = dict(frame)
    result['ts'] = ts.astimezone(timezone.utc).isoformat()
    for key, (low, high) in RANGES.items():
        value = frame.get(key)
        if value is not None and (type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high):
            raise ValueError(f'{key} outside validated range {low}..{high}')
        result[key] = value
    for key in SIGNALS:
        if frame.get(key) is not None and type(frame[key]) is not bool:
            raise ValueError(f'{key} must be boolean or null')
        result[key] = frame.get(key)
    if type(frame.get('stage', 1)) is not int or frame.get('stage', 1) not in (1, 2):
        raise ValueError('stage must be 1 or 2')
    result['stage'] = frame.get('stage', 1)
    return result, ts.timestamp()
