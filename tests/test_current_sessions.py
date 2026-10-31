"""Real local SQLite/process tests, with synthetic receiver messages; no board IO."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from contextlib import closing
from unittest.mock import patch
import threading
import types
from backend.acquisition import frames
from backend.current_sessions import initialize, reserve, reserve_for_source, MAX_SESSION
from backend.uno_transport import SnapshotReceiver

ROOT=Path(__file__).resolve().parents[1]
STAMP='2026-10-31T01:00:00Z'


def packet(session):
    return json.dumps(dict(seq=0,current_quality=dict(schema=1,session=session,max_age_us=5000,
        channels=[dict(channel=c,available=False,faults=2048,age_us=None,window_seq=None,
            timing_evidence_id=None,calibration_id=None,calibration_revision=None) for c in range(3)])),
        separators=(',',':'))


class CurrentSessions(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/'epochs.sqlite3'
        self.authority=initialize(self.path)

    def tearDown(self):self.temp.cleanup()

    def edit(self,sql,args=()):
        with closing(sqlite3.connect(self.path)) as db:
            db.execute(sql,args);db.commit()

    def test_committed_reservations_survive_reopen(self):
        self.assertEqual(reserve(self.path,self.authority),'1')
        self.assertEqual(reserve(Path(str(self.path)),self.authority),'2')
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT last_session FROM session_authority').fetchone()[0],2)

    def test_no_automatic_creation_reset_or_overwrite(self):
        reserve(self.path,self.authority)
        before=self.path.read_bytes()
        with self.assertRaises(FileExistsError):initialize(self.path)
        self.assertEqual(before,self.path.read_bytes())
        missing=self.path.parent/'missing.sqlite3'
        with self.assertRaises(sqlite3.Error):reserve(missing,self.authority)
        self.assertFalse(missing.exists())
        empty=self.path.parent/'empty.sqlite3';empty.touch()
        with self.assertRaises((ValueError,sqlite3.Error)):reserve(empty,self.authority)
        with self.assertRaises(FileExistsError):initialize(empty)

    def test_authority_and_schema_fail_closed(self):
        for authority in (None,'','A'*32,'f'*32,True):
            with self.subTest(authority=authority),self.assertRaises(ValueError):reserve(self.path,authority)
        self.assertEqual(reserve(self.path,self.authority),'1')
        self.edit('PRAGMA user_version=2')
        with self.assertRaises(ValueError):reserve(self.path,self.authority)

    def test_foreign_corrupt_and_replaced_database(self):
        other=self.path.parent/'other.sqlite3';other_authority=initialize(other)
        with self.assertRaises(ValueError):reserve(other,self.authority)
        self.assertEqual(reserve(other,other_authority),'1')
        bad=self.path.parent/'bad.sqlite3';bad.write_bytes(b'not sqlite')
        with self.assertRaises(sqlite3.Error):reserve(bad,self.authority)
        foreign=self.path.parent/'foreign.sqlite3'
        with closing(sqlite3.connect(foreign)) as db:
            db.execute('CREATE TABLE unrelated(x)');db.commit()
        with self.assertRaises(ValueError):reserve(foreign,self.authority)

    def test_invalid_rows_and_exhaustion_never_wrap(self):
        with closing(sqlite3.connect(self.path)) as db:
            db.execute('PRAGMA ignore_check_constraints=ON')
            db.execute('UPDATE session_authority SET last_session=-1')
            db.commit()
        with self.assertRaises(ValueError):reserve(self.path,self.authority)
        self.edit('UPDATE session_authority SET last_session=?',(MAX_SESSION-1,))
        self.assertEqual(reserve(self.path,self.authority),str(MAX_SESSION))
        with self.assertRaises(ValueError):reserve(self.path,self.authority)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT last_session FROM session_authority').fetchone()[0],MAX_SESSION)

    def test_write_lock_failure_returns_no_fallback_id(self):
        with closing(sqlite3.connect(self.path,isolation_level=None)) as db:
            db.execute('BEGIN IMMEDIATE')
            with self.assertRaises(sqlite3.OperationalError):reserve(self.path,self.authority,timeout=0.02)
            db.execute('ROLLBACK')
        self.assertEqual(reserve(self.path,self.authority),'1')

    def test_simultaneous_process_reservations_are_unique(self):
        command=[sys.executable,'-m','scripts.current_sessions','reserve',str(self.path),'--authority',self.authority]
        workers=[subprocess.Popen(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) for _ in range(6)]
        values=[]
        try:
            for process in workers:
                output,error=process.communicate(timeout=20)
                self.assertEqual(process.returncode,0,error)
                values.append(int(json.loads(output)['session']))
        finally:
            for process in workers:
                if process.poll() is None:process.kill();process.wait()
        self.assertEqual(sorted(values),list(range(1,7)))

    def test_process_exit_before_commit_or_after_unused_commit(self):
        uncommitted="import sqlite3,sys,os;d=sqlite3.connect(sys.argv[1],isolation_level=None);d.execute('BEGIN IMMEDIATE');d.execute('UPDATE session_authority SET last_session=50');os._exit(0)"
        subprocess.run([sys.executable,'-c',uncommitted,str(self.path)],check=True,cwd=ROOT,timeout=10)
        self.assertEqual(reserve(self.path,self.authority),'1')
        committed="import sys,os;from backend.current_sessions import reserve;reserve(sys.argv[1],sys.argv[2]);os._exit(0)"
        subprocess.run([sys.executable,'-c',committed,str(self.path),self.authority],check=True,cwd=ROOT,timeout=10)
        self.assertEqual(reserve(self.path,self.authority),'3') # Lost response consumed 2 permanently.

    def test_source_pairing_and_cli_initializer(self):
        self.assertIsNone(reserve_for_source('uno'))
        for source,ledger,authority in [('pi',self.path,self.authority),('uno',self.path,None),('uno',None,self.authority)]:
            with self.assertRaises(ValueError):reserve_for_source(source,ledger,authority)
        self.assertEqual(reserve_for_source('uno',self.path,self.authority),'1')
        new=self.path.parent/'cli.sqlite3'
        result=subprocess.run([sys.executable,'-m','scripts.current_sessions','init',str(new)],cwd=ROOT,capture_output=True,text=True,check=True,timeout=10)
        self.assertEqual(reserve(new,json.loads(result.stdout)['authority']),'1')

    def test_receiver_requires_independent_expected_reservation(self):
        expected=reserve(self.path,self.authority)
        for receiver in (SnapshotReceiver(),SnapshotReceiver(expected_session='2')):
            with self.assertRaises(ValueError):receiver.receive(packet(expected),0,0,timestamp=STAMP)
        receiver=SnapshotReceiver(expected_session=expected)
        self.assertEqual(receiver.receive(packet(expected),0,0,timestamp=STAMP)['current_quality']['session'],expected)
        next_session=reserve(self.path,self.authority)
        with self.assertRaises(ValueError):SnapshotReceiver(expected_session=next_session).receive(packet(expected),0,0,timestamp=STAMP)
        self.assertEqual(SnapshotReceiver().receive('{"seq":0}',0,0,timestamp=STAMP)['source'],'hardware')
        for invalid in (0,True,'0','01','18446744073709551616'):
            with self.assertRaises(ValueError):SnapshotReceiver(expected_session=invalid)

    def test_actual_adapter_receives_reserved_id_without_configuring_board(self):
        expected=reserve(self.path,self.authority)
        calls=[]
        def call(method):calls.append(method);return packet(expected)
        bridge=types.SimpleNamespace(call=call)
        modules={'arduino':types.ModuleType('arduino'),
                 'arduino.app_utils':types.SimpleNamespace(Bridge=bridge)}
        with patch.dict(sys.modules,modules),patch('backend.acquisition.time.monotonic_ns',side_effect=[0,0]):
            stream=frames('uno',None,threading.Event(),current_session=expected)
            self.assertEqual(next(stream)['current_quality']['session'],expected)
            stream.close()
        self.assertEqual(calls,['hvac_snapshot']) # No session setter, ADC upload or actuator call.

    def test_cli_rejects_foreign_source_and_measurement_database_collision(self):
        for arguments in [
            ['--source','pi','--current-session-ledger',str(self.path),'--current-session-authority',self.authority],
            ['--source','uno','--db',str(self.path),'--current-session-ledger',str(self.path),'--current-session-authority',self.authority]]:
            result=subprocess.run([sys.executable,'-m','backend.server',*arguments],cwd=ROOT,capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0)
        self.assertEqual(reserve(self.path,self.authority),'1')


if __name__=='__main__':unittest.main()
