"""Explicit session-ledger setup/reservation. Does not connect to a board."""
import argparse
import json
import sqlite3
from backend.current_sessions import initialize, reserve


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('operation',choices=['init','reserve'])
    p.add_argument('path',help='Dedicated local SQLite ledger; never the measurement database')
    p.add_argument('--authority',help='Expected authority printed by init')
    args=p.parse_args()
    try:
        if args.operation=='init':
            if args.authority:p.error('init generates a fresh authority; it cannot import/reset one')
            result=dict(authority=initialize(args.path),last_session=0)
        else:result=dict(session=reserve(args.path,args.authority))
    except (ValueError,OSError,sqlite3.Error) as exc:p.error(str(exc))
    print(json.dumps(result),flush=True)


if __name__=='__main__':main()
