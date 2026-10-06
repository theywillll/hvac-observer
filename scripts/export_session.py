import argparse,json,sqlite3
from pathlib import Path
from backend.validation import RANGES,SIGNALS
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('database');p.add_argument('output');p.add_argument('--session');a=p.parse_args()
    with sqlite3.connect(a.database) as db:
        session=a.session or db.execute('SELECT id FROM sessions ORDER BY started DESC,rowid DESC LIMIT 1').fetchone()[0]
        rows=db.execute('SELECT payload FROM hvac_state WHERE session_id=? ORDER BY timestamp',(session,))
        with Path(a.output).open('w',encoding='utf-8') as f:
            for row in rows:
                values=json.loads(row[0])['values'];allowed=set(RANGES)|set(SIGNALS)|{'ts','seq','stage','source'}
                f.write(json.dumps({k:v for k,v in values.items() if k in allowed})+'\n')
