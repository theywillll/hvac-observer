"""Loopback-only standard-library demo/API server. No public deployment support."""
import argparse
import json
import logging
import threading
import time
import uuid
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
from .engine import Engine
from .storage import Store, ROOT
from .simulator import generate, SCENARIOS, demo_baseline
from .profiles import profiles, match
from .calibration import apply_calibration
from .validation import validate

class Application:
    def __init__(self, db, config, scenario, hardware=False, baseline=None):
        self.lock = threading.RLock()
        self.config = config
        self.engine = Engine(config, baseline if hardware else demo_baseline())
        self.store = Store(db, str(uuid.uuid4()), 'hardware' if hardware else 'simulator', config)
        self.latest = None
        self.received = None
        self.hardware = hardware
        self.scenario = scenario
        self.error = None
        self.stop = threading.Event()

    def ingest(self, frame):
        with self.lock:
            validate(frame)
            result = self.engine.update(apply_calibration(frame,self.config))
            result['raw_values'] = frame
            result['calibration'] = self.config.get('calibration',{})
            self.store.save(result)
            self.latest = result
            self.received = time.monotonic()
            self.error = None
            return result

    def status(self):
        with self.lock:
            stale = self.received is None or time.monotonic()-self.received > 15
            return dict(latest=self.latest, stale=stale, error=self.error, simulation=not self.hardware,
                        scenario=self.scenario, age_s=None if self.received is None else time.monotonic()-self.received)

def handler_for(app):
    class Handler(BaseHTTPRequestHandler):
        def respond(self, status, body, mime='application/json'):
            data = json.dumps(body,allow_nan=False).encode() if mime == 'application/json' else body
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)
            if parsed.path == '/api/status': return self.respond(200,app.status())
            if parsed.path == '/api/profiles': return self.respond(200,profiles())
            if parsed.path == '/api/profile':
                return self.respond(200, match(query.get('manufacturer',[''])[0],query.get('model',[''])[0]))
            if parsed.path == '/api/history':
                try: limit=max(1,min(2000,int(query.get('limit',['720'])[0])))
                except ValueError: return self.respond(400,{'error':'limit must be an integer'})
                with app.lock: data=app.store.history(limit)
                return self.respond(200,data)
            names = {'/':'index.html','/app.js':'app.js','/style.css':'style.css'}
            if parsed.path in names:
                name=names[parsed.path]
                mime={'html':'text/html; charset=utf-8','js':'text/javascript','css':'text/css'}[name.split('.')[-1]]
                return self.respond(200,(ROOT/'dashboard'/name).read_bytes(),mime)
            self.respond(404,{'error':'not found'})

        def do_POST(self):
            # Hardware is ingested inside the process, not via an unauthenticated write API.
            self.respond(405,{'error':'read-only API'})

        def log_message(self, fmt, *args): logging.debug(fmt,*args)
    return Handler

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--port',type=int,default=8765)
    p.add_argument('--db',default='hvac.sqlite3')
    p.add_argument('--config',default=str(ROOT/'config.example.json'))
    p.add_argument('--scenario',choices=SCENARIOS,default='dirty_filter')
    p.add_argument('--source',choices=['simulator','uno','pi','jsonl'],default='simulator')
    p.add_argument('--input',help='JSONL acquisition spool for jsonl source')
    p.add_argument('--baseline',help='Reviewed baseline JSON (hardware only)')
    p.add_argument('--speed',type=float,default=20,help='Simulation speed multiplier')
    args=p.parse_args()
    if args.speed <= 0: p.error('speed must be positive')
    config=json.loads(Path(args.config).read_text())
    baseline=json.loads(Path(args.baseline).read_text()) if args.baseline else None
    if args.source != 'simulator' and baseline and baseline.get('synthetic'):
        p.error('Synthetic baselines cannot be used with hardware')
    app=Application(args.db,config,args.scenario,args.source!='simulator',baseline)
    def worker():
        try:
            if args.source=='simulator':
                stream=generate(args.scenario,count=10**8,start=datetime.now(timezone.utc))
            else:
                from .acquisition import frames
                stream=frames(args.source,args.input,app.stop)
            for frame in stream:
                if app.stop.is_set(): break
                app.ingest(frame)
                if args.source=='simulator' and app.stop.wait(5/args.speed): break
        except Exception as exc:
            logging.exception('Acquisition stopped')
            app.error=str(exc)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),handler_for(app))
    thread=threading.Thread(target=worker,daemon=True)
    thread.start()
    print(f'HVAC Observer: http://127.0.0.1:{args.port} | source={args.source}',flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        app.stop.set()
        server.server_close()
        thread.join(timeout=3)
        if not thread.is_alive(): app.store.close()

if __name__=='__main__': main()
