import copy
import json
import math
import tempfile
import threading
import unittest
from datetime import datetime, timezone, timedelta
from pathlib import Path
from http.server import ThreadingHTTPServer
from urllib.request import urlopen, Request
from urllib.error import HTTPError
from backend.engine import Engine, mode_of
from backend.validation import validate
from backend.simulator import generate, demo_baseline, SCENARIOS
from backend.baseline import fit, compare
from backend.storage import Store, ROOT
from backend.server import Application, handler_for
from backend.acquisition import crc8, decode_sht
from backend.profiles import profiles, match
from backend.calibration import apply_calibration
from backend.features import ct_rms, vibration_rms

class Rules(unittest.TestCase):
    def run_scenario(self,name):
        e=Engine(baseline=demo_baseline());found=set();last=None
        for f in generate(name,count=900):
            last=e.update(f);found.update(a['id'] for a in last['alerts'])
        return found,last

    def test_normals_have_no_faults(self):
        for s in ('normal_cooling','normal_heating'):
            found,_=self.run_scenario(s);self.assertFalse(found,(s,found))

    def test_fault_scenarios(self):
        expected={'dirty_filter':'filter_restriction','frozen_evaporator':'freeze_symptoms','short_cycling':'short_cycling','failed_condenser_fan':'condenser_fan_response','blower_degradation':'blower_degradation','compressor_current_anomaly':'current_vibration','no_response':'no_response','humidity_degradation':'humidity_degradation','condensate':'condensate'}
        for s,key in expected.items():
            with self.subTest(s=s):self.assertIn(key,self.run_scenario(s)[0])

    def test_no_synthetic_baseline_in_engine_default(self):
        e=Engine()
        for f in generate('dirty_filter',count=300):r=e.update(f)
        self.assertNotIn('filter_restriction',[a['id'] for a in r['alerts']])
        self.assertIsNone(r['metrics']['anomaly_score'])

    def test_missing_current_not_no_response(self):
        e=Engine()
        for f in generate('no_response',count=300):
            f['compressor_a']=None;r=e.update(f)
        self.assertNotIn('no_response',[a['id'] for a in r['alerts']])

    def test_startup_transient_suppressed(self):
        e=Engine()
        for f in generate('normal_cooling',count=40):
            f['supply_c']=25;r=e.update(f)
        self.assertFalse(r['alerts'])

    def test_gap_resets_persistence(self):
        e=Engine();frames=list(generate('no_response',count=30))
        for f in frames:e.update(f)
        f=copy.deepcopy(frames[-1]);f['seq']+=1;f['ts']='2026-01-01T01:00:00+00:00'
        r=e.update(f);self.assertTrue(r['gap_detected']);self.assertFalse(r['alerts'])

    def test_critical_clears_when_float_clears(self):
        e=Engine()
        for f in generate('condensate',count=150):r=e.update(f)
        self.assertEqual(r['health'],'Critical')
        f=copy.deepcopy(f);f['seq']+=1;f['ts']=(datetime.fromisoformat(f['ts'])+timedelta(seconds=5)).isoformat();f['condensate']=False
        r=e.update(f);self.assertNotEqual(r['health'],'Critical')

    def test_heat_pump_ob_polarity(self):
        f,_=validate(next(generate()));f.update(Y=True,OB=True)
        self.assertEqual(mode_of(f,{'equipment_type':'heat_pump','ob_energized_in_cooling':False})[0],'heating')
        f['OB']=None;self.assertEqual(mode_of(f,{'equipment_type':'heat_pump'})[0],'unknown')

    def test_defrost_suppresses_cooling_fault(self):
        e=Engine()
        for f in generate('frozen_evaporator',count=300):f['defrost']=True;r=e.update(f)
        self.assertFalse(r['alerts'])

    def test_unknown_mode_not_idle(self):
        f,_=validate({'ts':'2026-01-01T00:00:00Z','seq':0,'source':'hardware'})
        self.assertEqual(mode_of(f,{})[0],'unknown')

    def test_energy_integration(self):
        e=Engine();start=datetime(2026,1,1,tzinfo=timezone.utc)
        for i in range(721):
            f=next(generate());f.update(seq=i,ts=(start+timedelta(seconds=i*5)).isoformat(),power_w=1000)
            r=e.update(f)
        self.assertAlmostEqual(r['metrics']['energy_kwh'],1)

    def test_null_power_is_not_estimated_from_amps(self):
        e=Engine()
        for f in generate(count=30):f['power_w']=None;r=e.update(f)
        self.assertIsNone(r['metrics']['energy_kwh'])

    def test_duplicate_rejected(self):
        e=Engine();f=next(generate());e.update(f)
        with self.assertRaises(ValueError):e.update(f)

    def test_stage_change_restarts_settling(self):
        e=Engine()
        for f in generate(count=100):r=e.update(f)
        self.assertTrue(r['steady'])
        f=copy.deepcopy(f);f['seq']+=1;f['ts']=(datetime.fromisoformat(f['ts'])+timedelta(seconds=5)).isoformat();f['stage']=2
        self.assertFalse(e.update(f)['steady'])

class Validation(unittest.TestCase):
    def test_calibration_retains_input(self):
        f=next(generate());original=f['return_c']
        out=apply_calibration(f,{'calibration':{'return_c':{'gain':1,'offset':-.2}}})
        self.assertEqual(f['return_c'],original);self.assertAlmostEqual(out['return_c'],original-.2)

    def test_waveform_rms_and_clipping(self):
        volts=[1.65+.333*math.sqrt(2)*math.sin(2*math.pi*60*i/8000) for i in range(8000)]
        self.assertAlmostEqual(ct_rms(volts,50),50,places=5)
        volts[0]=3.3
        with self.assertRaises(ValueError):ct_rms(volts,50)

    def test_vibration_gravity_removed(self):
        samples=[(0,0,1+.1*math.sin(2*math.pi*i/20)) for i in range(1000)]
        self.assertAlmostEqual(vibration_rms(samples),.1/math.sqrt(2))

    def test_reject_nonfinite_bool_and_bounds(self):
        for value in (math.nan,math.inf,True,-1000):
            f=next(generate());f['return_c']=value
            with self.assertRaises(ValueError):validate(f)

    def test_reject_naive_timestamp(self):
        f=next(generate());f['ts']='2026-01-01T00:00:00'
        with self.assertRaises(ValueError):validate(f)

    def test_crc(self):
        self.assertEqual(crc8([0xBE,0xEF]),0x92)
        data=[0x66,0x66,0,0x80,0,0];data[2]=crc8(data[:2]);data[5]=crc8(data[3:5]);t,h=decode_sht(data)
        self.assertAlmostEqual(t,25,places=1);self.assertAlmostEqual(h,50,places=1)
        data[0]=0
        with self.assertRaises(ValueError):decode_sht(data)

    def test_timestamp_normalized(self):
        f=next(generate());f['ts']='2026-01-01T05:30:00+05:30';r,_=validate(f)
        self.assertEqual(r['ts'],'2026-01-01T00:00:00+00:00')

class Data(unittest.TestCase):
    def test_profile_provenance_and_unknowns(self):
        src={s['id'] for s in json.loads((ROOT/'data/sources.json').read_text())}
        ps=profiles();self.assertGreaterEqual(len({p['manufacturer'] for p in ps}),14)
        for p in ps:
            for v in p['parameters'].values():
                if v['value'] is not None:self.assertIn(v['source_id'],src)
                else:self.assertEqual(v['provenance'],'unknown')

    def test_match_does_not_inherit_family_limits(self):
        self.assertIsNone(match('Carrier','24SCA524W003'))
        self.assertIsNotNone(match('Carrier','24SCA5'))

    def test_baseline_condition_matching(self):
        b=demo_baseline();f=list(generate(count=100))[-1];f['split_c']=f['supply_c']-f['return_c'];f['outdoor_c']=-20
        self.assertIsNone(compare(b,f,'cooling')[2])

    def test_persistence_and_indexes(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as td:
            s=Store(str(Path(td)/'db.sqlite3'),'test','simulator',{});e=Engine()
            for f in generate(count=5):s.save(e.update(f))
            self.assertEqual(len(s.history()),5)
            self.assertTrue(s.db.execute('PRAGMA foreign_keys').fetchone()[0])
            s.close()

class API(unittest.TestCase):
    def test_http_dashboard_readonly_and_bad_query(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as td:
            a=Application(str(Path(td)/'db'),{},'normal_cooling')
            a.ingest(next(generate()))
            server=ThreadingHTTPServer(('127.0.0.1',0),handler_for(a));thread=threading.Thread(target=server.serve_forever);thread.start()
            base=f'http://127.0.0.1:{server.server_port}'
            try:
                self.assertIn(b'HVAC Observer',urlopen(base).read())
                self.assertFalse(json.load(urlopen(base+'/api/status'))['stale'])
                self.assertEqual(len(json.load(urlopen(base+'/api/history'))),1)
                with self.assertRaises(HTTPError) as ex:urlopen(base+'/api/history?limit=no')
                self.assertEqual(ex.exception.code,400)
                with self.assertRaises(HTTPError) as ex:urlopen(Request(base+'/api/ingest',data=b'{}'))
                self.assertEqual(ex.exception.code,405)
                a.received-=30;self.assertTrue(a.status()['stale'])
            finally:server.shutdown();server.server_close();thread.join();a.store.close()

if __name__=='__main__':unittest.main()
