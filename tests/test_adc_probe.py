"""Synthetic CSV fixtures test probe analysis, not an ADC or physical timing."""
import io
import unittest
from scripts.check_adc_probe import analyze, UINT32_MAX


def fixture(rows, marker=True):
    prefix = '# hvac_adc_probe; bits=12; sweep_ms=100; raw_only\n' if marker else ''
    return io.StringIO(prefix + 'sweep,channel,start_us,read_us,raw,status\n' +
                       '\n'.join(','.join(map(str, r)) for r in rows) + '\n')


def nominal():
    return [[s, c, 1000 + s * 100000 + c * 20, 10, 2048, 'ok']
            for s in range(2) for c in range(3)]


class ADCProbe(unittest.TestCase):
    def test_separate_channel_intervals(self):
        rows = nominal()
        rows[4][2] += 3
        result = analyze(fixture(rows))
        self.assertEqual([c['interval_us_min'] for c in result['channels']],
                         [100000, 100003, 100000])
        self.assertEqual(result['sweeps'], 2)
        self.assertNotIn('amps', result)

    def test_errors_are_not_zero_current(self):
        rows = nominal()
        rows[0][4:] = [-5, 'driver_error']
        rows[1][4:] = [4096, 'invalid_code']
        report = analyze(fixture(rows))
        self.assertEqual(report['channels'][0]['driver_errors'], 1)
        self.assertEqual(report['channels'][0]['raw_min'], 2048)
        self.assertEqual(report['channels'][1]['invalid_codes'], 1)

    def test_near_rail_boundaries(self):
        rows = nominal()
        for row, raw in zip(rows, [0, 8, 9, 4086, 4087, 4095]):
            row[4:] = [raw, 'near_rail' if raw <= 8 or raw >= 4087 else 'ok']
        self.assertEqual(sum(c['near_rail'] for c in analyze(fixture(rows))['channels']), 4)

    def test_timer_and_sequence_rollover(self):
        rows = nominal()
        for row in rows:
            row[0] = (UINT32_MAX + row[0]) & UINT32_MAX
            row[2] = (UINT32_MAX - 50 + row[2] - 1000) & UINT32_MAX
        result = analyze(fixture(rows))
        self.assertEqual([c['interval_us_min'] for c in result['channels']], [100000] * 3)

    def test_rejects_missing_channels_and_sweeps(self):
        for rows in [[], nominal()[:-1], nominal()[1:], nominal()[:3] +
                     [[3, c, 301000+c*20, 10, 2048, 'ok'] for c in range(3)]]:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                analyze(fixture(rows))

    def test_rejects_reset_and_multiple_startups(self):
        rows = nominal()
        for row in rows[3:]: row[0] = 0
        with self.assertRaises(ValueError): analyze(fixture(rows))
        mixed = fixture(nominal()).getvalue() + fixture(nominal()).getvalue()
        with self.assertRaises(ValueError): analyze(io.StringIO(mixed))

    def test_rejects_invalid_status_or_brackets(self):
        for field, value in [(5, 'near_rail'), (2, 1005), (3, -1), (3, 1 << 31)]:
            rows = nominal(); rows[1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(fixture(rows))

    def test_rejects_truncated_extra_or_repeated_header(self):
        for suffix in ['0,0,1000,10,2048\n', '0,0,1000,10,2048,ok,extra\n',
                       'sweep,channel,start_us,read_us,raw,status\n']:
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                analyze(io.StringIO('sweep,channel,start_us,read_us,raw,status\n'+suffix))
