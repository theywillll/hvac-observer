"""Consume native C++ synthetic JSONL fixtures and validate with the real backend."""
import json
import sys
import math
from backend.validation import validate
from backend.current_quality import CURRENT_FIELDS
from backend.uno_transport import SnapshotReceiver


def check(lines):
    expected={'default','pending','valid','boundary','expired','full_cycle',
              'stopped','failed','clock','reset','future','maximum','decimal','zero'}
    seen=set(); max_bytes=0
    for line in lines:
        item=json.loads(line)
        name,payload=item['fixture'],item['payload']
        if name not in expected or name in seen:raise ValueError('Unexpected/duplicate fixture')
        seen.add(name)
        # Hardware source exercises strict validation; all measurements are
        # explicitly synthetic test data, never claimed as a board capture.
        frame=dict(payload,ts='2026-10-21T01:00:00Z',seq=0,source='hardware')
        out,_=validate(frame)
        # Exercise the real bounded UNO receiver too. Each fixture is an
        # independent synthetic stream; no physical RPC latency is asserted.
        raw=json.dumps(dict(payload,seq=0,stage=1),separators=(',',':'),allow_nan=False)
        received=SnapshotReceiver().receive(raw,0,0,timestamp=frame['ts'])
        if received != dict(out,stage=1):raise ValueError('Receiver/validator disagreement')
        values=[out[k] for k in CURRENT_FIELDS]
        if name in ('valid','boundary'):
            if values != [1,2,3]:raise ValueError('Numeric round trip failed')
            q=out['current_quality']
            if q['session']!='18446744073709551615':raise ValueError('Session precision lost')
            if q['channels'][0]['age_us'] != (5000 if name=='boundary' else 1000):
                raise ValueError('Request-time age mismatch')
        elif name=='decimal':
            if not math.isclose(values[0],0.1,rel_tol=1e-15) or values[1:]!=[None,None]:
                raise ValueError('Decimal round trip failed')
        elif name=='zero':
            if values!=[0,None,None]:raise ValueError('Qualified synthetic zero lost')
        elif values != [None,None,None]:raise ValueError('Unavailable value became numeric')
        encoded=json.dumps(payload,separators=(',',':'),allow_nan=False).encode('ascii')
        max_bytes=max(max_bytes,len(encoded))
    if seen != expected:raise ValueError('Missing fixtures')
    return len(seen),max_bytes


if __name__=='__main__':
    count,size=check(sys.stdin)
    print(f'{count} synthetic C++/Python wire fixtures passed; largest re-encoded payload {size} bytes.')
