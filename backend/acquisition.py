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

def frames(source, path, stop):
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
        last_seq=None
        while not stop.is_set():
            frame=json.loads(Bridge.call('hvac_snapshot'))
            if frame.get('seq')==last_seq:
                raise ValueError('MCU acquisition stalled')
            last_seq=frame['seq']
            frame['ts']=datetime.now(timezone.utc).isoformat()
            frame['source']='hardware'
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
