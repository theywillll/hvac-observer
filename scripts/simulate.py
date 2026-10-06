import argparse,json
from backend.simulator import generate,SCENARIOS
p=argparse.ArgumentParser();p.add_argument('--scenario',choices=SCENARIOS,default='normal_cooling');p.add_argument('--count',type=int,default=900);p.add_argument('--output',required=True)
if __name__=='__main__':
    a=p.parse_args()
    with open(a.output,'w',encoding='utf-8') as f:
        for frame in generate(a.scenario,count=a.count): f.write(json.dumps(frame)+'\n')
