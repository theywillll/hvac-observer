"""Versioned, condition-matched robust baselines. Training is explicit."""
import math
import statistics

FEATURES = ('split_c', 'filter_pa', 'compressor_a', 'blower_a', 'vibration_g', 'power_w', 'airflow_proxy')
FLOORS = dict(zip(FEATURES, (0.6, 3.0, 0.5, 0.2, 0.02, 100, 0.05)))

def bucket(frame, mode):
    values = [frame.get(x) for x in ('outdoor_c', 'return_c', 'indoor_rh')]
    if any(x is None for x in values):
        return None
    return f"{mode}:{frame['stage']}:{math.floor(values[0]/5)}:{math.floor(values[1]/3)}:{math.floor(values[2]/10)}"

def fit(rows, minimum=30):
    """Rows must be reviewed steady-state, healthy commissioning samples."""
    groups = {}
    for frame, mode in rows:
        key = bucket(frame, mode)
        if key is None:
            continue
        for name in FEATURES:
            value = frame.get(name)
            if value is not None:
                groups.setdefault(key, {}).setdefault(name, []).append(value)
    output = {}
    for key, fields in groups.items():
        output[key] = {}
        for name, values in fields.items():
            if len(values) < minimum:
                continue
            median = statistics.median(values)
            scale = max(FLOORS[name], 1.4826 * statistics.median(abs(x-median) for x in values))
            output[key][name] = {'median': median, 'scale': scale, 'n': len(values)}
    return {'version': 1, 'method': 'median/MAD', 'buckets': output, 'minimum_samples': minimum}

def compare(model, frame, mode):
    fields = model.get('buckets', {}).get(bucket(frame, mode), {})
    scores = {name: abs(frame[name]-entry['median'])/entry['scale'] for name, entry in fields.items() if frame.get(name) is not None}
    return fields, scores, min(1.0, max(scores.values(), default=0)/6.0) if scores else None
