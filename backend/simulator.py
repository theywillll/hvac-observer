"""Deterministic synthetic scenarios, never field evidence."""
import random
from datetime import datetime, timezone, timedelta
from .baseline import fit
SCENARIOS = ('normal_cooling','normal_heating','dirty_filter','frozen_evaporator','short_cycling','failed_condenser_fan','blower_degradation','compressor_current_anomaly','no_response','humidity_degradation','condensate')

def generate(scenario='normal_cooling', count=900, step=5, seed=7, start=None):
    if scenario not in SCENARIOS:
        raise ValueError('Unknown scenario')
    rng = random.Random(seed)
    start = start or datetime(2026, 1, 1, tzinfo=timezone.utc)
    for i in range(count):
        s = i*step
        on = s % 1800 >= 120
        if scenario == 'short_cycling':
            on = s % 240 >= 120
        heating = scenario == 'normal_heating'
        demand = on
        comp = on and not heating and scenario != 'no_response'
        f = dict(ts=(start+timedelta(seconds=s)).isoformat(), seq=i, source='simulator', stage=1,
                 return_c=25+rng.uniform(-.1,.1), supply_c=(45 if heating and on else 15 if comp else 25)+rng.uniform(-.1,.1),
                 outdoor_c=32+rng.uniform(-.1,.1), indoor_rh=52+rng.uniform(-.1,.1), supply_rh=80 if comp else 52,
                 filter_pa=40+rng.uniform(-1,1) if on else 0, static_pa=100 if on else 0,
                 compressor_a=9+rng.uniform(-.2,.2) if comp else 0, blower_a=2+rng.uniform(-.1,.1) if on else 0,
                 condenser_fan_a=.8 if comp else 0, power_w=2300 if comp else 300 if on else 8,
                 voltage_v=230, vibration_g=.12+rng.uniform(-.01,.01) if comp else 0,
                 suction_c=7 if comp else 25, liquid_c=40 if comp else 25, outdoor_unit_c=45 if comp else 32,
                 airflow_proxy=1 if on else 0, R=True, Y=demand and not heating, Y2=False,
                 W=demand and heating, W2=False, G=on, OB=False, condensate=False, defrost=False)
        if s >= 600 and on:
            if scenario == 'dirty_filter': f.update(filter_pa=65, supply_c=12)
            if scenario == 'frozen_evaporator': f.update(suction_c=-3, airflow_proxy=.4, supply_c=8)
            if scenario == 'failed_condenser_fan': f.update(condenser_fan_a=0, outdoor_unit_c=75)
            if scenario == 'blower_degradation': f.update(airflow_proxy=.45, blower_a=1.2)
            if scenario == 'compressor_current_anomaly': f.update(compressor_a=14, vibration_g=.4, power_w=3300)
            if scenario == 'humidity_degradation': f.update(indoor_rh=70+(s%1800)/1800)
            if scenario == 'condensate': f['condensate'] = True
        yield f

def demo_baseline():
    rows = []
    for f in generate(count=1440):
        if f['Y'] and f['seq']*5 % 1800 > 300:
            f['split_c'] = f['supply_c']-f['return_c']
            rows.append((f, 'cooling'))
    model = fit(rows)
    model['synthetic'] = True
    return model
