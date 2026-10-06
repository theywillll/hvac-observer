import argparse,json,sqlite3
from pathlib import Path
from backend.storage import ROOT
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('database');a=p.parse_args()
    with sqlite3.connect(a.database) as db:
        db.executescript((ROOT/'data/schema.sql').read_text())
        for s in json.loads((ROOT/'data/sources.json').read_text()):
            db.execute('INSERT OR REPLACE INTO sources VALUES(?,?,?,?,?,?)',(s['id'],s['url'],s['document'],s.get('version'),s['accessed'],s.get('locator')))
        for profile in json.loads((ROOT/'data/equipment_profiles.json').read_text()):
            for name,param in profile['parameters'].items():
                db.execute('INSERT OR REPLACE INTO profile_parameters VALUES(?,?,?,?,?,?,?)',(profile['id'],name,json.dumps(param['value']),param.get('unit'),param['provenance'],param.get('source_id'),param.get('qualifier')))
    print('Database and cited profiles initialized.')
