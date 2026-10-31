"""All current records below are synthetic wire-contract fixtures."""
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from backend.validation import validate
from backend.calibration import apply_calibration
from backend.current_quality import CURRENT_FIELDS
from backend.server import Application
from backend.uno_transport import SnapshotReceiver


def qualified_frame():
    return dict(ts='2026-10-21T01:00:00Z', seq=0, source='hardware',
                compressor_a=1.0, blower_a=2.0, condenser_fan_a=3.0,
                current_quality=dict(schema=1, session='18446744073709551615', max_age_us=5000,
                    channels=[dict(channel=c, available=True, faults=0, age_us=1000,
                        window_seq='0', timing_evidence_id=7, calibration_id=9+c,
                        calibration_revision=2) for c in range(3)]))


class CurrentQuality(unittest.TestCase):
    def test_valid_metadata_and_copy(self):
        f=qualified_frame(); out,_=validate(f)
        self.assertEqual(out['current_quality'],f['current_quality'])
        f['current_quality']['channels'][0]['age_us']=9999
        self.assertEqual(out['current_quality']['channels'][0]['age_us'],1000)
        self.assertEqual(out['current_quality']['session'],'18446744073709551615')

    def test_hardware_requires_quality_for_numbers(self):
        for key in CURRENT_FIELDS:
            f=dict(ts='2026-10-21T01:00:00Z',seq=0,source='hardware'); f[key]=0
            with self.subTest(key=key),self.assertRaises(ValueError): validate(f)
        f=dict(ts='2026-10-21T01:00:00Z',seq=0,source='hardware')
        self.assertIsNone(validate(f)[0]['compressor_a'])

    def test_synthetic_legacy_compatibility(self):
        for source in ('simulator','replay'):
            f=qualified_frame(); f.pop('current_quality'); f['source']=source
            self.assertEqual(validate(f)[0]['compressor_a'],1)

    def test_schema_structure_and_extra_fields(self):
        mutations=[lambda q:q.update(schema=True),lambda q:q.update(schema=2),
            lambda q:q.update(extra=1),lambda q:q.pop('session'),
            lambda q:q.update(channels=q['channels'][:2]),
            lambda q:q.update(channels=q['channels'][::-1]),
            lambda q:q['channels'][0].update(extra=0),
            lambda q:q['channels'][0].update(channel=False),
            lambda q:q['channels'][0].update(available=1)]
        for mutate in mutations:
            f=qualified_frame(); mutate(f['current_quality'])
            with self.assertRaises(ValueError): validate(f)
        f=qualified_frame(); f['current_quality']=None
        with self.assertRaises(ValueError): validate(f)

    def test_identifiers_are_canonical_and_bounded(self):
        bad=(0,True,'','01','-1','1.0',' 1','١','18446744073709551616','9'*100)
        for value in bad:
            for key in ('session','window_seq'):
                f=qualified_frame()
                if key=='session':f['current_quality'][key]=value
                else:f['current_quality']['channels'][0][key]=value
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):validate(f)
        f=qualified_frame(); f['current_quality']['session']='0'
        with self.assertRaises(ValueError):validate(f)
        f=qualified_frame(); f['current_quality']['channels'][0]['window_seq']='18446744073709551615'
        self.assertEqual(validate(f)[0]['current_quality']['channels'][0]['window_seq'],'18446744073709551615')

    def test_stale_missing_or_invalid_qualification(self):
        bad_updates=[dict(faults=128),dict(age_us=5001),dict(age_us=None),dict(age_us=True),
            dict(age_us=-1),dict(age_us=2**32-1),dict(window_seq=None),
            dict(timing_evidence_id=None),dict(timing_evidence_id=0),
            dict(timing_evidence_id=True),dict(calibration_id=2**32),
            dict(calibration_revision=None),dict(calibration_revision=-1),
            dict(faults=2**14),dict(faults=True),dict(faults=-1)]
        for update in bad_updates:
            f=qualified_frame(); f['current_quality']['channels'][0].update(update)
            with self.subTest(update=update),self.assertRaises(ValueError):validate(f)
        f=qualified_frame(); f['current_quality']['channels'][0]['age_us']=5000
        validate(f)

    def test_unavailable_requires_null_and_reason(self):
        f=qualified_frame(); c=f['current_quality']['channels'][0]
        c.update(available=False,faults=32)
        with self.assertRaises(ValueError):validate(f)
        f['compressor_a']=None; validate(f)
        c['faults']=0
        with self.assertRaises(ValueError):validate(f)
        c.update(faults=32,age_us=5001)
        with self.assertRaises(ValueError):validate(f)
        c['faults']|=128; validate(f)
        c['age_us']=None; validate(f)

    def test_unobserved_and_unconfigured_states(self):
        f=qualified_frame()
        for key in CURRENT_FIELDS:f[key]=None
        for c in f['current_quality']['channels']:
            c.update(available=False,faults=2048,age_us=None,window_seq=None,
                     timing_evidence_id=None,calibration_id=None,calibration_revision=None)
        validate(f)
        q=f['current_quality']; q.update(session=None,max_age_us=0)
        with self.assertRaises(ValueError):validate(f)
        for c in q['channels']:c['faults']=1
        validate(f)
        q['channels'][0]['window_seq']='0'
        with self.assertRaises(ValueError):validate(f)

    def test_age_policy_bounds(self):
        for value in (0,-1,True,2**31,None):
            f=qualified_frame();f['current_quality']['max_age_us']=value
            with self.subTest(value=value),self.assertRaises(ValueError):validate(f)

    def test_cannot_calibrate_current_twice(self):
        f=qualified_frame()
        for key in CURRENT_FIELDS:
            with self.assertRaises(ValueError):
                apply_calibration(f,{'calibration':{key:{'gain':1,'offset':0}}})
        f['return_c']=25
        out=apply_calibration(f,{'calibration':{'return_c':{'gain':1,'offset':-0.5}}})
        self.assertEqual(out['return_c'],24.5)
        self.assertEqual(f['return_c'],25)
        self.assertEqual(out['compressor_a'],1)

    def test_application_preserves_quality_and_nulls_in_history(self):
        with tempfile.TemporaryDirectory() as td:
            app=Application(str(Path(td)/'db'),{},'normal_cooling',hardware=True)
            try:
                receiver=SnapshotReceiver(expected_session="18446744073709551615")
                def ingest(f):
                    raw={k:v for k,v in f.items() if k not in ('ts','source')}
                    received=receiver.receive(json.dumps(raw,separators=(',',':')),0,0,timestamp=f['ts'])
                    with patch('backend.server.time.monotonic_ns',return_value=0):return app.ingest(received)
                f=qualified_frame(); result=ingest(f)
                self.assertEqual(result['values']['current_quality'],f['current_quality'])
                f=copy.deepcopy(f);f.update(seq=1,ts='2026-10-21T01:00:05Z',compressor_a=None)
                f['current_quality']['channels'][0].update(available=False,faults=128,age_us=6000)
                result=ingest(f)
                self.assertIsNone(result['values']['compressor_a'])
                history=app.store.history()
                self.assertEqual(len(history),2)
                self.assertEqual(history[-1]['values']['current_quality']['channels'][0]['faults'],128)
                json.dumps(history,allow_nan=False)
            finally:app.store.close()


if __name__=='__main__':unittest.main()
