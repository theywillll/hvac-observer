"""Synthetic JSON fixtures validate a format, never an ADC or sample timing."""
import io
import json
import unittest
from scripts.check_adc_sequence import HEADER, analyze, UINT32_MAX


def row(number=0, start=1000, status=0, raw=True):
    return dict(request=number, start_us=start, read_us=16000,
                driver_status=status, guard_ok=True,
                raw=[100+c+10*s for s in range(16) for c in range(3)] if raw else None)


def fixture(*rows, header=None):
    return io.StringIO('\n'.join(json.dumps(r) for r in [HEADER if header is None else header, *rows]))


class ADCSequence(unittest.TestCase):
    def test_channel_order_and_unknown_sample_timing(self):
        r = analyze(fixture(row(), row(1, 101000)))
        self.assertEqual([c['raw_min'] for c in r['channels']], [100, 101, 102])
        self.assertEqual([c['raw_max'] for c in r['channels']], [250, 251, 252])
        self.assertEqual([c['samples'] for c in r['channels']], [32]*3)
        self.assertEqual(r['request_interval_us_min'], 100000)
        self.assertIsNone(r['measured_sample_interval_us'])
        self.assertFalse(r['sample_timing_verified'])
        self.assertNotIn('amps', r)

    def test_error_busy_guard_and_unwritten_buffers_discarded(self):
        rows = [row(0, 1000, -16, False), row(1, 101000, -5, False),
                row(2, 201000, 0, False), row(3, 301000, 0, False), row(4, 401000)]
        rows[2]['guard_ok'] = False
        r = analyze(fixture(*rows))
        self.assertEqual((r['accepted_requests'], r['rejected_requests']), (1, 4))
        self.assertEqual((r['driver_errors'], r['guard_failures'], r['unpublished_buffers']), (2, 1, 1))
        self.assertEqual(r['channels'][0]['samples'], 16)

    def test_raw_data_on_driver_failure_is_rejected(self):
        for status, guard in [(-16, True), (1, True), (0, False)]:
            r = row(status=status); r['guard_ok'] = guard
            with self.assertRaises(ValueError): analyze(fixture(r))

    def test_malformed_lengths_codes_and_types(self):
        for bad in [[], [0]*47, [0]*49, [True]*48, [16384]*48,
                    [-1]*48, [1.5]*48, 'codes']:
            r = row(); r['raw'] = bad
            with self.assertRaises(ValueError): analyze(fixture(r))
        for key, value in [('request', True), ('start_us', -1), ('read_us', 1 << 31),
                           ('driver_status', False), ('driver_status', 1 << 31), ('guard_ok', 1)]:
            r = row(); r[key] = value
            with self.assertRaises(ValueError): analyze(fixture(r))

    def test_session_header_reset_and_missing_requests(self):
        for changed in [dict(HEADER, setup_status=-19), dict(HEADER, resolution_bits=12),
                        dict(HEADER, version=True), dict(HEADER, adc_channels=[9, 11, 10])]:
            with self.assertRaises(ValueError): analyze(fixture(row(), header=changed))
        for rows in [(row(1),), (row(), row(2, 101000)), (row(), row()), (row(), HEADER)]:
            with self.assertRaises(ValueError): analyze(fixture(*rows))
        with self.assertRaises(ValueError): analyze(fixture())

    def test_bracket_overlap_backwards_and_timer_rollover(self):
        for start in [999, 1001, 16999]:
            with self.assertRaises(ValueError): analyze(fixture(row(), row(1, start)))
        r = analyze(fixture(row(0, UINT32_MAX - 50000), row(1, 49999)))
        self.assertEqual(r['request_interval_us_max'], 100000)

    def test_near_rail_counts_native_fourteen_bits(self):
        r = row(); r['raw'][:6] = [0, 32, 33, 16383, 16351, 16350]
        report = analyze(fixture(r))
        self.assertEqual([c['near_rail'] for c in report['channels']], [2, 2, 0])

    def test_duplicate_json_keys_partial_lines_and_extra_fields(self):
        for text in ['{"request":0,"request":1}', '{', '[]']:
            with self.assertRaises(ValueError):
                analyze(io.StringIO(json.dumps(HEADER)+'\n'+text))
        r = row(); r['sample_interval_us'] = 1000
        with self.assertRaises(ValueError): analyze(fixture(r))

    def test_no_usable_samples_stay_unknown(self):
        r = analyze(fixture(row(status=-5, raw=False)))
        self.assertEqual(r['accepted_requests'], 0)
        self.assertIsNone(r['channels'][0]['raw_min'])
        self.assertEqual(r['channels'][0]['samples'], 0)
