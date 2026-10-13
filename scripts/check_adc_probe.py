"""Validate one UNO Q slow-probe session, without estimating volts or amperes."""
import argparse
import csv
import json
from pathlib import Path

FIELDS = ('sweep', 'channel', 'start_us', 'read_us', 'raw', 'status')
UINT32_MAX = (1 << 32) - 1
CODE_MAX = (1 << 12) - 1
RAIL_MARGIN = 8


def _status(raw):
    if raw < 0:
        return 'driver_error'
    if raw > CODE_MAX:
        return 'invalid_code'
    return 'near_rail' if raw <= RAIL_MARGIN or raw >= CODE_MAX - RAIL_MARGIN else 'ok'


def _lines(lines):
    startup_seen = False
    for line in lines:
        if line.strip().startswith('# hvac_adc_probe'):
            if startup_seen:
                raise ValueError('Multiple startup markers: analyze reset sessions separately')
            startup_seen = True
        elif line.strip() and not line.lstrip().startswith('#'):
            yield line


def analyze(lines):
    reader = csv.DictReader(_lines(lines))
    if tuple(reader.fieldnames or ()) != FIELDS:
        raise ValueError('Expected the exact six-column ADC probe header')
    channels = [dict(samples=0, driver_errors=0, invalid_codes=0, near_rail=0,
                     raw_min=None, raw_max=None, read_us_min=None, read_us_max=None,
                     interval_us_min=None, interval_us_max=None) for _ in range(3)]
    previous_start = [None] * 3
    previous_sweep = None
    previous_in_sweep = None
    count = 0
    for row in reader:
        if None in row or any(row.get(k) is None for k in FIELDS):
            raise ValueError('Truncated row or unexpected CSV columns')
        try:
            sweep, channel, start, elapsed, raw = (int(row[k]) for k in FIELDS[:-1])
        except ValueError as exc:
            raise ValueError('Invalid integer or repeated header in probe session') from exc
        if channel != count % 3:
            raise ValueError('Expected one complete ordered A0/A1/A2 sweep')
        if any(not 0 <= v <= UINT32_MAX for v in (sweep, start, elapsed)):
            raise ValueError('Sweep and timing fields must be unsigned 32-bit values')
        if elapsed >= 1 << 31:
            raise ValueError('Read duration is ambiguous across timer rollover')
        if channel == 0:
            if previous_sweep is not None and sweep != (previous_sweep + 1) & UINT32_MAX:
                raise ValueError('Missing/repeated sweep or reset: analyze sessions separately')
            previous_sweep = sweep
            previous_in_sweep = None
        elif sweep != previous_sweep:
            raise ValueError('Channel sweep numbers do not agree')
        if previous_in_sweep is not None:
            delta = (start - previous_in_sweep[0]) & UINT32_MAX
            if delta >= 1 << 31 or delta < previous_in_sweep[1]:
                raise ValueError('Read-call brackets overlap or run backwards')
        previous_in_sweep = (start, elapsed)
        if row['status'] != _status(raw):
            raise ValueError('Reported status contradicts the raw ADC code')
        stats = channels[channel]
        stats['samples'] += 1
        flag = row['status']
        if flag in ('driver_error', 'invalid_code'):
            stats['driver_errors' if flag == 'driver_error' else 'invalid_codes'] += 1
        else:
            if flag == 'near_rail':
                stats['near_rail'] += 1
            for key, fn in [('raw_min', min), ('raw_max', max)]:
                stats[key] = raw if stats[key] is None else fn(stats[key], raw)
        for key, fn in [('read_us_min', min), ('read_us_max', max)]:
            stats[key] = elapsed if stats[key] is None else fn(stats[key], elapsed)
        if previous_start[channel] is not None:
            interval = (start - previous_start[channel]) & UINT32_MAX
            if not 0 < interval < 1 << 31:
                raise ValueError('Channel timestamps repeat or run backwards')
            for key, fn in [('interval_us_min', min), ('interval_us_max', max)]:
                stats[key] = interval if stats[key] is None else fn(stats[key], interval)
        previous_start[channel] = start
        count += 1
    if not count or count % 3:
        raise ValueError('No complete sweep or truncated final sweep')
    return {'raw_only': True, 'adc_bits_requested': 12, 'sweeps': count // 3,
            'channels': channels,
            'limits': 'Call brackets are not ADC aperture timestamps; no RMS, calibration or rate guarantee.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path, help='One captured current_adc_probe session')
    args = parser.parse_args()
    try:
        with args.csv.open(encoding='utf-8-sig', newline='') as f:
            report = analyze(f)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
