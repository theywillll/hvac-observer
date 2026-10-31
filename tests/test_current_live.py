"""Synthetic receipt clocks. No hardware timing or browser real-time claim."""
import copy
import json
import tempfile
import unittest
import threading
from http.server import ThreadingHTTPServer
from urllib.request import urlopen
from pathlib import Path
from unittest.mock import patch
from backend.server import Application, handler_for
from backend.uno_transport import SnapshotReceiver
from backend.current_live import live_projection


def frame(ages=(1000,2000,3000)):
    raw=dict(seq=0,stage=1,compressor_a=2,blower_a=1,condenser_fan_a=0.3,
        current_quality=dict(schema=1,session='1',max_age_us=5000,channels=[
            dict(channel=c,available=True,faults=0,age_us=a,window_seq='0',
                 timing_evidence_id=7,calibration_id=9+c,calibration_revision=2)
            for c,a in enumerate(ages)]))
    return SnapshotReceiver(expected_session="1").receive(json.dumps(raw,separators=(',',':')),0,0,
                                     timestamp='2026-10-26T01:00:00Z')


class CurrentLive(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.app=Application(str(Path(self.temp.name)/'db'),{},'normal_cooling',hardware=True)

    def tearDown(self):
        self.app.store.close();self.temp.cleanup()

    def ingest(self,f=None,now=0):
        with patch('backend.server.time.monotonic_ns',return_value=now):
            return self.app.ingest(frame() if f is None else f)

    def status(self,now):
        with patch('backend.server.time.monotonic_ns',return_value=now):return self.app.status()

    def test_deadline_context_not_json_and_mutation_rejected(self):
        f=frame();self.assertNotIn('current_receipt',json.dumps(f))
        with self.assertRaises(ValueError):self.ingest(dict(f))
        self.assertIsNone(self.app.latest)
        f['compressor_a']=3
        with self.assertRaises(ValueError):self.ingest(f)
        self.assertEqual(self.app.store.history(),[])

    def test_no_receipt_needed_for_environment_or_simulation(self):
        f=dict(ts='2026-10-26T01:00:00Z',seq=0,source='hardware',return_c=25)
        self.ingest(f);self.assertIsNone(self.status(0)['latest']['values']['compressor_a'])
        f.update(ts='2026-10-26T01:00:01Z',seq=1,source='simulator',compressor_a=3)
        self.ingest(f);self.assertEqual(self.status(10**10)['latest']['values']['compressor_a'],3)

    def test_independent_expiry_and_boundary(self):
        self.ingest()
        s=self.status(2000000);self.assertEqual(s['latest']['values']['condenser_fan_a'],0.3)
        s=self.status(2000001)
        self.assertIsNone(s['latest']['values']['condenser_fan_a'])
        self.assertEqual(s['latest']['values']['compressor_a'],2)
        self.assertEqual(s['current_live']['channels'][2]['reason'],'expired')
        self.assertEqual(s['latest']['health'],'Unknown')
        s=self.status(4000001)
        self.assertIsNone(s['latest']['values']['compressor_a'])
        self.assertEqual(s['latest']['values']['current_quality']['channels'][0]['faults'],128)

    def test_expiry_on_ingestion_not_forwarded_to_engine_as_zero(self):
        result=self.ingest(now=4000001)
        self.assertIsNone(result['values']['compressor_a'])
        self.assertIsNone(result['compressor_state'])
        self.assertEqual(result['raw_values']['compressor_a'],2)

    def test_status_never_changes_history_engine_or_retained_source(self):
        self.ingest()
        before=copy.deepcopy(self.app.latest);history=self.app.store.history()
        engine_before=copy.deepcopy(self.app.engine.last_f)
        s=self.status(4000001);s['latest']['values']['return_c']=999
        self.assertEqual(self.app.latest,before)
        self.assertEqual(self.app.store.history(),history)
        self.assertEqual(self.app.engine.last_f,engine_before)
        self.assertEqual(history[0]['values']['compressor_a'],2)
        self.assertNotIn('raw_values',self.status(4000002)['latest'])

    def test_error_clears_immediately_and_retains_float_warning(self):
        self.ingest()
        self.app.latest['alerts']=[dict(id='high_current',severity='Warning'),dict(id='condensate',severity='Critical')]
        self.app.acquisition_failed(RuntimeError('Bridge stopped'))
        s=self.status(1)
        self.assertFalse(s['stale'])
        self.assertIsNone(s['latest']['values']['compressor_a'])
        self.assertEqual(s['error'],'Bridge stopped')
        self.assertEqual(s['latest']['health'],'Critical')
        self.assertEqual([a['id'] for a in s['latest']['alerts']],['condensate'])
        self.assertTrue(s['latest']['alerts'][0]['last_observation'])
        self.assertIsNone(s['latest']['metrics']['compressor_runtime_s'])

    def test_backward_clock_latches_until_successful_ingestion(self):
        self.ingest();self.status(1000)
        self.assertIsNone(self.status(999)['latest']['values']['compressor_a'])
        self.assertIsNone(self.status(1001)['latest']['values']['compressor_a'])

    def test_receipt_clock_and_old_wire_data_cannot_rebase(self):
        with self.assertRaises(ValueError):self.ingest(now=-1)
        self.ingest(now=3000000)
        self.assertEqual(self.status(3000000)['current_live']['channels'][0]['remaining_us'],1000)
        self.assertIsNone(self.status(4000001)['latest']['values']['compressor_a'])

    def test_stale_or_error_masks_legacy_simulator_current(self):
        f=dict(ts='2026-10-26T01:00:00Z',seq=0,source='simulator',compressor_a=3)
        self.ingest(f);self.app.received-=16
        self.assertIsNone(self.status(0)['latest']['values']['compressor_a'])

    def test_http_live_view_and_history_are_distinct(self):
        self.ingest()
        server=ThreadingHTTPServer(('127.0.0.1',0),handler_for(self.app))
        thread=threading.Thread(target=server.serve_forever);thread.start()
        base=f'http://127.0.0.1:{server.server_port}'
        try:
            status=json.load(urlopen(base+'/api/status')) # Real host clock is past the synthetic lease.
            self.assertIsNone(status['latest']['values']['compressor_a'])
            self.assertNotIn('raw_values',status['latest'])
            history=json.load(urlopen(base+'/api/history'))
            self.assertEqual(history[0]['values']['compressor_a'],2)
            self.assertIn(b'function liveCurrentView',urlopen(base+'/current-view.js').read())
        finally:server.shutdown();server.server_close();thread.join()


if __name__=='__main__':unittest.main()
