import json
import sqlite3
from pathlib import Path
from .validation import RANGES
ROOT = Path(__file__).resolve().parents[1]

def units(k):
    return next((v for suffix,v in [('_c','degC'),('_rh','%RH'),('_pa','Pa'),('_a','A'),('_w','W'),('_v','V'),('_g','g')] if k.endswith(suffix)), 'relative')

class Store:
    def __init__(self, path, session, source, config):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.executescript((ROOT/'data/schema.sql').read_text())
        self.session = session
        self.db.execute('INSERT OR IGNORE INTO equipment VALUES(?,?,?,?,?,?)', ('installation',config.get('manufacturer'),config.get('model'),config.get('equipment_type','unknown'),json.dumps(config),'[]'))
        self.db.execute('INSERT INTO sessions VALUES(?,?,datetime(\'now\'),?)', (session,'installation',source))
        for name in RANGES:
            self.db.execute('INSERT OR IGNORE INTO sensors VALUES(?,?,?,?,?)', (name,'installation',name,name,json.dumps(config.get('calibration',{}).get(name,{}))))
        self.db.commit()

    def save(self, result):
        f = result['values']
        with self.db:
            for k in RANGES:
                raw = result.get('raw_values',f).get(k)
                quality = 'missing' if f[k] is None else 'calibrated' if k in result.get('calibration',{}) else 'unverified_calibration'
                self.db.execute('INSERT INTO measurements VALUES(?,?,?,?,?,?,?)', (self.session,f['ts'],k,raw,f[k],units(k),quality))
            self.db.execute('INSERT INTO hvac_state VALUES(?,?,?,?,?,?,?,?)', (self.session,f['ts'],f['seq'],json.dumps({k:f[k] for k in ('Y','Y2','W','W2','G','OB')}),result['compressor_state'],result['fan_state'],result['mode'],json.dumps(result,allow_nan=False)))
            for a in result['new_alerts']:
                self.db.execute('INSERT INTO alerts(session_id,timestamp,severity,rule_id,anomaly_score,confidence,evidence) VALUES(?,?,?,?,?,?,?)', (self.session,f['ts'],a['severity'],a['id'],a['anomaly_score'],a['confidence'],json.dumps(a)))

    def history(self, limit=720):
        rows = self.db.execute('SELECT payload FROM hvac_state WHERE session_id=? ORDER BY timestamp DESC LIMIT ?', (self.session,limit)).fetchall()
        return [json.loads(r[0]) for r in reversed(rows)]

    def prune(self, cutoff):
        with self.db:
            for table in ('measurements','hvac_state','alerts'):
                self.db.execute(f'DELETE FROM {table} WHERE timestamp < ?', (cutoff,))

    def close(self): self.db.close()
