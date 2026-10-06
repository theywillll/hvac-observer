"""Explicit offline training. User is responsible for reviewing input health."""
import argparse,json
from pathlib import Path
from backend.engine import Engine
from backend.baseline import fit
from backend.validation import validate

def train(path,config,allow_synthetic=False):
    engine=Engine(config);rows=[];first=None;last=None
    for line in Path(path).read_text().splitlines():
        f=json.loads(line);_,t=validate(f)
        if f['source']=='simulator' and not allow_synthetic: raise ValueError('Synthetic training requires --allow-synthetic')
        if first is None:first=t
        last=t
        r=engine.update(f)
        if r['steady'] and not r['alerts'] and r['quality']=='good' and r['mode'] in ('cooling','heating'):
            rows.append((r['values'],r['mode']))
    if not allow_synthetic and (first is None or last-first<7*86400): raise ValueError('Collect at least seven days; two weeks recommended')
    model=fit(rows);model.update(synthetic=allow_synthetic,learning_period={'start_epoch':first,'end_epoch':last},review_required=True)
    return model

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('input');p.add_argument('output');p.add_argument('--config',default='config.example.json');p.add_argument('--allow-synthetic',action='store_true');p.add_argument('--reviewed-healthy',action='store_true');a=p.parse_args()
    if not a.reviewed_healthy:p.error('Review commissioning data before supplying --reviewed-healthy')
    Path(a.output).write_text(json.dumps(train(a.input,json.loads(Path(a.config).read_text()),a.allow_synthetic),indent=2))
