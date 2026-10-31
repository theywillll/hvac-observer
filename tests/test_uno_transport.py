"""Synthetic clocks/messages; no real Bridge, ADC or board timing evidence."""
import json
import sys
import threading
import types
import unittest
from unittest.mock import patch
from backend.uno_transport import SnapshotReceiver, MAX_SNAPSHOT_BYTES
from backend.acquisition import frames

STAMP='2026-10-23T02:00:00Z'


def payload():
    return dict(seq=0, stage=1, compressor_a=1, blower_a=2, condenser_fan_a=3,
        current_quality=dict(schema=1,session='1',max_age_us=5000,channels=[
            dict(channel=c,available=True,faults=0,age_us=1000,window_seq='0',
                 timing_evidence_id=7,calibration_id=9+c,calibration_revision=2)
            for c in range(3)]))


def receive(receiver, frame, start=0, end=0):
    return receiver.receive(json.dumps(frame,separators=(',',':')),start,end,timestamp=STAMP)


class UnoTransport(unittest.TestCase):
    def test_legacy_and_exact_size(self):
        raw='{"seq":0,"return_c":25}'
        r=SnapshotReceiver(expected_session="1").receive(raw+' '*(MAX_SNAPSHOT_BYTES-len(raw)),0,0,timestamp=STAMP)
        self.assertEqual(r['source'],'hardware');self.assertEqual(r['return_c'],25)
        self.assertIsNone(r['compressor_a'])
        with self.assertRaises(ValueError):
            SnapshotReceiver(expected_session="1").receive(raw+' '*(MAX_SNAPSHOT_BYTES-len(raw)+1),0,0,timestamp=STAMP)

    def test_strict_json_and_host_owned_fields(self):
        invalid=[None,b'{"seq":0}',[], '{}','[]','null','{"seq":0,',
            '{"seq":0,"seq":1}','{"seq":0,"x":{"a":1,"a":2}}',
            '{"seq":0,"return_c":NaN}','{"seq":0,"return_c":Infinity}',
            '{"seq":0,"return_c":-Infinity}','{"seq":true}',
            '{"seq":-1}','{"seq":4294967296}',
            '{"seq":0,"ts":"2026-01-01T00:00:00Z"}',
            '{"seq":0,"source":"simulator"}','{"seq":0,"extra":"é"}',
            '{"seq":0,"return_c":1e999}']
        for raw in invalid:
            with self.subTest(raw=raw),self.assertRaises(ValueError):
                SnapshotReceiver(expected_session="1").receive(raw,0,0,timestamp=STAMP)

    def test_frame_sequence_reset_and_wrap(self):
        r=SnapshotReceiver(expected_session="1");receive(r,dict(seq=5))
        for seq in (5,4,0):
            with self.assertRaises(ValueError):receive(r,dict(seq=seq))
        receive(r,dict(seq=9)) # Observed frame gaps are allowed.
        receive(r,dict(seq=2**32-1))
        with self.assertRaises(ValueError):receive(r,dict(seq=0))

    def test_clock_contract_and_atomic_retry(self):
        r=SnapshotReceiver(expected_session="1");receive(r,dict(seq=0),10,20)
        for start,end in ((19,30),(30,29),(-1,30),(True,30),(30,2**64)):
            with self.subTest(start=start,end=end),self.assertRaises(ValueError):
                receive(r,dict(seq=1),start,end)
        receive(r,dict(seq=1),20,30)

    def test_round_trip_budget_boundary(self):
        f=payload();receive(SnapshotReceiver(expected_session="1"),f,0,4000000)
        with self.assertRaises(ValueError):receive(SnapshotReceiver(expected_session="1"),f,0,4000001)
        f['current_quality']['channels'][0]['age_us']=5000
        receive(SnapshotReceiver(expected_session="1"),f,0,0)
        with self.assertRaises(ValueError):receive(SnapshotReceiver(expected_session="1"),f,0,1)

    def test_same_window_cannot_renew_lease_or_move_age_backward(self):
        r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f,0,1000)
        f['seq']=1;receive(r,f,2000000,2001000) # Same frozen age never extends first deadline.
        f['seq']=2
        with self.assertRaises(ValueError):receive(r,f,4000001,4000001)
        r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
        f['seq']=1;f['current_quality']['channels'][0]['age_us']=999
        with self.assertRaises(ValueError):receive(r,f,1000,1000)

    def test_same_ordinal_is_immutable_and_cannot_revive(self):
        for update in ('value','timing'):
            r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
            f['seq']=1
            if update=='value':f['compressor_a']=1.1
            else:f['current_quality']['channels'][0]['timing_evidence_id']=8
            with self.assertRaises(ValueError):receive(r,f)
        r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
        f['seq']=1;f['compressor_a']=None
        f['current_quality']['channels'][0].update(available=False,faults=32)
        receive(r,f)
        restored=payload();restored['seq']=2
        with self.assertRaises(ValueError):receive(r,restored)
        restored['current_quality']['channels'][0]['window_seq']='1'
        receive(r,restored)

    def test_new_ordinals_and_channels_are_independent(self):
        r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
        f['seq']=1;f['current_quality']['channels'][0].update(window_seq='10',age_us=1000)
        f['current_quality']['channels'][1]['age_us']=3000
        receive(r,f,2000000,2000000)
        f['seq']=2;f['current_quality']['channels'][0]['window_seq']='9'
        with self.assertRaises(ValueError):receive(r,f,2000000,2000000)
        f['current_quality']['channels'][0]['window_seq']='11'
        receive(r,f,2000000,2000000)
        f['seq']=3
        with self.assertRaises(ValueError):receive(r,f,4000001,4000001) # Old channel 1 lease expired.

    def test_session_policy_and_calibration_changes_require_restart(self):
        for change in ('session','missing','policy','calibration'):
            r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
            f['seq']=1
            if change=='session':f['current_quality']['session']='2'
            elif change=='policy':f['current_quality']['max_age_us']=6000
            elif change=='calibration':
                f['current_quality']['channels'][0].update(window_seq='1',calibration_revision=3)
            else:
                f.pop('current_quality');f.update(compressor_a=None,blower_a=None,condenser_fan_a=None)
            with self.subTest(change=change),self.assertRaises(ValueError):receive(r,f)
        r=SnapshotReceiver(expected_session="1");receive(r,dict(seq=0))
        f=payload();f['seq']=1;receive(r,f) # One-way initialization is allowed.

    def test_unavailable_windows_need_no_numeric_lease(self):
        f=payload()
        for key,c in zip(('compressor_a','blower_a','condenser_fan_a'),f['current_quality']['channels']):
            f[key]=None;c.update(available=False,faults=128,age_us=6000)
        r=SnapshotReceiver(expected_session="1");receive(r,f,0,10**12)
        f['seq']=1
        for c in f['current_quality']['channels']:
            c.update(window_seq=None,age_us=None,timing_evidence_id=None,
                     calibration_id=None,calibration_revision=None,faults=2048)
        with self.assertRaises(ValueError):receive(r,f,10**12,10**12)

    def test_rejected_frame_does_not_partially_consume_channels(self):
        r=SnapshotReceiver(expected_session="1");f=payload();receive(r,f)
        f['seq']=1
        for c in f['current_quality']['channels']:c['window_seq']='1'
        f['current_quality']['channels'][2]['calibration_revision']=3
        with self.assertRaises(ValueError):receive(r,f,1,2)
        f['current_quality']['channels'][2]['calibration_revision']=2
        f['compressor_a']=1.1
        receive(r,f,1,2) # Would be an illegal mutation if channel 0 had committed.

    def test_real_adapter_uses_request_clock_and_receiver(self):
        bridge=types.SimpleNamespace(call=lambda method:json.dumps(dict(seq=0,return_c=25)))
        modules={'arduino':types.ModuleType('arduino'),
                 'arduino.app_utils':types.SimpleNamespace(Bridge=bridge)}
        with patch.dict(sys.modules,modules),patch('backend.acquisition.time.monotonic_ns',side_effect=[1,2,3,4]):
            stream=frames('uno',None,threading.Event())
            first=next(stream);self.assertEqual(first['return_c'],25)
            # Avoid the polling wait; the second repeated frame fails before yield.
            with patch('threading.Event.wait',return_value=False),self.assertRaises(ValueError):next(stream)
            stream.close()


if __name__=='__main__':unittest.main()
