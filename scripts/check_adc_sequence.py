"""Inspect one raw sequence-probe session; never infer sample timing or amperes."""
import argparse
import json
from pathlib import Path

UINT32_MAX = (1 << 32) - 1
HEADER = dict(type='hvac_adc_sequence', version=1, resolution_bits=14,
              adc_channels=[9, 10, 11], sweeps=16, requested_interval_us=1000,
              setup_status=0)
FIELDS = {'request', 'start_us', 'read_us', 'driver_status', 'guard_ok', 'raw'}


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def _uint(value):
    return type(value) is int and 0 <= value <= UINT32_MAX


def analyze(lines):
    header_seen = False
    previous = None
    channels = [dict(adc_channel=9+c, samples=0, raw_min=None, raw_max=None,
                     near_rail=0) for c in range(3)]
    report = dict(requests=0, accepted_requests=0, rejected_requests=0,
                  driver_errors=0, guard_failures=0, unpublished_buffers=0,
                  read_us_min=None, read_us_max=None,
                  request_interval_us_min=None, request_interval_us_max=None,
                  requested_sweep_interval_us=1000, measured_sample_interval_us=None,
                  sample_timing_verified=False, channels=channels)
    for line in lines:
        if not line.strip():
            continue
        try:
            row = json.loads(line, object_pairs_hook=_object)
        except (ValueError, TypeError) as exc:
            raise ValueError('Expected one complete JSON object per line') from exc
        if type(row) is not dict:
            raise ValueError('Expected a JSON object')
        if not header_seen:
            # Equality alone would accept True as 1. Compare types as well.
            if (row != HEADER or any(type(row.get(k)) is not type(v)
                                     for k, v in HEADER.items()) or
                    any(type(v) is not int for v in row.get('adc_channels', []))):
                raise ValueError('Expected exact successful sequence startup header; inspect setup_status')
            header_seen = True
            continue
        if set(row) != FIELDS:
            raise ValueError('Unexpected fields or repeated startup: separate reset sessions')
        if not all(_uint(row[k]) for k in ('request', 'start_us', 'read_us')):
            raise ValueError('Request and timing fields must be uint32 integers')
        if row['read_us'] >= 1 << 31:
            raise ValueError('Ambiguous request duration')
        status = row['driver_status']
        if type(status) is not int or not -(1 << 31) <= status < 1 << 31:
            raise ValueError('Driver status must be int32')
        if type(row['guard_ok']) is not bool:
            raise ValueError('guard_ok must be boolean')
        if previous is None:
            if row['request'] != 0:
                raise ValueError('Capture must start at request zero with its startup header')
        else:
            if row['request'] != (previous['request'] + 1) & UINT32_MAX:
                raise ValueError('Missing/repeated request or reset')
            delta = (row['start_us'] - previous['start_us']) & UINT32_MAX
            if delta >= 1 << 31 or delta < previous['read_us']:
                raise ValueError('Request brackets overlap or run backwards')
            report['request_interval_us_min'] = _min(report['request_interval_us_min'], delta)
            report['request_interval_us_max'] = _max(report['request_interval_us_max'], delta)
        previous = row
        report['requests'] += 1
        elapsed = row['read_us']
        report['read_us_min'] = _min(report['read_us_min'], elapsed)
        report['read_us_max'] = _max(report['read_us_max'], elapsed)
        raw = row['raw']
        if status != 0 or not row['guard_ok'] or raw is None:
            if raw is not None:
                raise ValueError('Failed/guard-damaged requests must not publish sample data')
            report['rejected_requests'] += 1
            report['driver_errors'] += status != 0
            report['guard_failures'] += not row['guard_ok']
            report['unpublished_buffers'] += status == 0 and row['guard_ok']
            continue
        if (type(raw) is not list or len(raw) != 48 or
                any(type(v) is not int or not 0 <= v <= 16383 for v in raw)):
            raise ValueError('Expected 48 native 14-bit codes in sweep/channel order')
        report['accepted_requests'] += 1
        for c, stats in enumerate(channels):
            values = raw[c::3]
            stats['samples'] += len(values)
            stats['raw_min'] = _min(stats['raw_min'], min(values))
            stats['raw_max'] = _max(stats['raw_max'], max(values))
            stats['near_rail'] += sum(v <= 32 or v >= 16383 - 32 for v in values)
    if not header_seen or not report['requests']:
        raise ValueError('Expected startup plus at least one complete request')
    return report


def _min(old, new):
    return new if old is None else min(old, new)


def _max(old, new):
    return new if old is None else max(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    try:
        with args.capture.open(encoding='utf-8') as source:
            print(json.dumps(analyze(source), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(2, f'Invalid sequence capture: {exc}\n')


if __name__ == '__main__':
    main()
