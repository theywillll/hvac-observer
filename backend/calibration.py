"""Linear sensor calibration in engineering units; provenance is installer supplied."""
import math
from .validation import RANGES
from .current_quality import CURRENT_FIELDS

def apply_calibration(frame, config):
    calibrated=dict(frame)
    for key, entry in config.get('calibration',{}).items():
        if key not in RANGES: raise ValueError('Unknown calibration channel: '+key)
        if key in CURRENT_FIELDS and 'current_quality' in frame:
            raise ValueError('Current is calibrated by the MCU contract; remove server calibration: '+key)
        gain, offset=entry.get('gain',1),entry.get('offset',0)
        if type(gain) not in (int,float) or type(offset) not in (int,float) or not math.isfinite(gain) or not math.isfinite(offset) or gain<=0:
            raise ValueError('Invalid calibration: '+key)
        value=frame.get(key)
        if value is not None:calibrated[key]=gain*value+offset
    return calibrated
