"""Retention command; takes an explicit UTC cutoff. Back up before pruning."""
import argparse,sqlite3
from datetime import datetime
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('database');p.add_argument('--before',required=True);a=p.parse_args();dt=datetime.fromisoformat(a.before)
    if dt.utcoffset() is None:p.error('Timezone required')
    with sqlite3.connect(a.database) as db:
        for table in ('measurements','hvac_state','alerts'):db.execute(f'DELETE FROM {table} WHERE timestamp < ?',(a.before,))
