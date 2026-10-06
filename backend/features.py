"""Signal feature helpers for optional, independently timed instrument nodes."""
import math
import statistics

def ct_rms(volts, rated_a, rated_v=.333, low_rail=.02, high_rail=3.28):
    """Return RMS A, or reject clipping. Volts must be uniformly sampled externally."""
    if len(volts)<100 or rated_a<=0 or rated_v<=0:
        raise ValueError('Need >=100 valid waveform samples and positive calibration')
    if any(not math.isfinite(v) or v<=low_rail or v>=high_rail for v in volts):
        raise ValueError('ADC waveform clipped or invalid; RMS unavailable')
    mean=statistics.fmean(volts)
    return math.sqrt(statistics.fmean((v-mean)**2 for v in volts))*rated_a/rated_v

def vibration_rms(samples):
    """Dynamic vector g RMS after per-axis mean removal; fixed mounting/bandwidth."""
    if len(samples)<100 or any(len(s)!=3 or not all(math.isfinite(x) for x in s) for s in samples):
        raise ValueError('Need >=100 finite 3-axis samples')
    center=[statistics.fmean(s[i] for s in samples) for i in range(3)]
    return math.sqrt(statistics.fmean(sum((s[i]-center[i])**2 for i in range(3)) for s in samples))

def airflow_ratio(pressure_pa, reference_pa):
    """Only for a commissioned fixed flow element, never arbitrary filter pressure."""
    if pressure_pa<0 or reference_pa<=0:raise ValueError('Positive calibrated flow pressure required')
    return math.sqrt(pressure_pa/reference_pa)
