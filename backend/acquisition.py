"""Hardware adapters: optional smbus2 on Pi; Arduino App Lab Bridge on UNO Q."""
import json
import time
from datetime import datetime, timezone

def crc8(data):
    crc=0xFF
    for b in data:
        crc ^= b
        for _ in range(8): crc=((crc << 1)^0x31)&255 if crc&128 else (crc << 1)&255
    return crc

def decode_sht(data):
    if len(data)!=6 or crc8(data[:2])!=data[2] or crc8(data[3:5])!=data[5]:
        raise ValueError('SHT31 CRC failure')
    return -45+175*((data[0]<<8)|data[1])/65535, 100*((data[3]<<8)|data[4])/65535

def frames(source, path, stop, *, current_session_config=None):
    if source=='jsonl':
        if not path: raise ValueError('--input is required')
        with open(path,encoding='utf-8') as f:
            while not stop.is_set():
                line=f.readline()
                if line:
                    frame=json.loads(line)
                    # Reject stale spool contents rather than making an old measurement look live.
                    age=(datetime.now(timezone.utc)-datetime.fromisoformat(frame['ts'])).total_seconds()
                    if not -5 <= age <= 15: raise ValueError('Stale or future acquisition spool')
                    yield frame
                else: stop.wait(.2)
        return
    if source=='uno':
        from arduino.app_utils import Bridge
        from .uno_transport import SnapshotReceiver
        session = None
        call = Bridge.call
        if current_session_config is not None:
            from .current_handshake import (
                discover_current_boot, negotiate_current_session, renew_current_session,
            )

            def call(method, *args):
                if stop.is_set():
                    raise InterruptedError("UNO current acquisition stopped")
                return Bridge.call(method, *args, timeout=2.0)

            if stop.is_set():
                return
            boot = discover_current_boot(call)
            def exchange(request):
                return call("hvac_current_session", request)

            session = negotiate_current_session(exchange, boot, *current_session_config)
        receiver=SnapshotReceiver(expected_session=session)
        while not stop.is_set():
            if session is not None:
                renew_current_session(exchange, boot, session)
            started_ns=time.monotonic_ns()
            raw=call('hvac_snapshot')
            received_ns=time.monotonic_ns()
            frame=receiver.receive(raw,started_ns,received_ns,
                timestamp=datetime.now(timezone.utc).isoformat())
            yield frame
            stop.wait(2)
        return
    from smbus2 import SMBus, i2c_msg
    with SMBus(1) as bus:
        seq=0
        while not stop.is_set():
            f=dict(ts=datetime.now(timezone.utc).isoformat(),seq=seq,source='hardware',stage=1)
            for addr,tk,hk in [(0x44,'return_c','indoor_rh'),(0x45,'supply_c','supply_rh')]:
                try:
                    bus.i2c_rdwr(i2c_msg.write(addr,[0x24,0x00]))
                    time.sleep(.02)
                    r=i2c_msg.read(addr,6)
                    bus.i2c_rdwr(r)
                    f[tk],f[hk]=decode_sht(list(r))
                except (OSError, ValueError):
                    f[tk]=f[hk]=None
            # Other adapters publish through JSONL; unconnected channels remain unknown.
            yield f
            seq+=1
            stop.wait(2)
