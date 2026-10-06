"""Time-aware diagnostics. Severity is independent of heuristic confidence."""
from collections import deque
from .baseline import compare
from .validation import validate

SEVERITY = {'Normal': 0, 'Warning': 1, 'Probable fault': 2, 'Critical': 3}

def mode_of(f, config):
    comp = None if f['compressor_a'] is None else f['compressor_a'] > config.get('compressor_on_a', 1.0)
    fan = None if f['blower_a'] is None else f['blower_a'] > config.get('blower_on_a', .25)
    if f['defrost']:
        return 'defrost', comp, fan
    if f['W'] or f['W2']:
        return 'heating', comp, fan
    if f['Y'] or f['Y2']:
        if config.get('equipment_type') == 'heat_pump':
            if f['OB'] is None:
                return 'unknown', comp, fan
            return ('cooling' if f['OB'] == config.get('ob_energized_in_cooling', True) else 'heating'), comp, fan
        return 'cooling', comp, fan
    if comp:
        if f['supply_c'] is None or f['return_c'] is None:
            return 'unknown', comp, fan
        d = f['supply_c']-f['return_c']
        return ('cooling' if d < -2 else 'heating' if d > 2 else 'unknown'), comp, fan
    if fan or f['G']:
        return 'fan_only', comp, fan
    if comp is False and fan is False:
        return 'idle', comp, fan
    return 'unknown', comp, fan

class Engine:
    def __init__(self, config=None, baseline=None):
        self.config = config or {}
        self.baseline = baseline or {}
        self.last_t = None
        self.last_f = None
        self.last_mode = None
        self.mode_since = None
        self.comp_since = None
        self.demand_since = None
        self.response_s = None
        self.conditions = {}
        self.active = {}
        self.starts = deque()
        self.cycles = deque(maxlen=1000)
        self.history = deque(maxlen=7200)
        self.total_runtime = 0
        self.energy_wh = 0
        self.energy_observed_s = 0
        self.daily_runtime = 0
        self.day = None

    def update(self, raw):
        f, t = validate(raw)
        if self.last_t is not None and t <= self.last_t:
            raise ValueError('Duplicate or out-of-order timestamp')
        if self.last_f and f['seq'] <= self.last_f['seq']:
            raise ValueError('Non-increasing sequence; restart acquisition session after device reset')
        gap = self.last_t is not None and t-self.last_t > self.config.get('max_gap_s', 15)
        dt = 0 if self.last_t is None or gap else t-self.last_t
        mode, comp, fan = mode_of(f, self.config)
        if gap:
            self.conditions.clear()
            self.comp_since = None
            self.demand_since = None
            self.response_s = None
            self.starts.clear()
            self.cycles.clear()
            self.history.clear()
        if mode != self.last_mode or gap or (self.last_f and f['stage'] != self.last_f['stage']):
            self.mode_since = t
        steady = mode in ('cooling', 'heating') and t-self.mode_since >= self.config.get('settle_s', 180)
        previous_comp = self.last_f is not None and self.last_f['compressor_a'] is not None and self.last_f['compressor_a'] > self.config.get('compressor_on_a', 1)
        if comp and not previous_comp and not gap:
            # First observation is left-censored, not a witnessed start.
            if self.last_f is not None:
                self.starts.append(t)
                self.comp_since = t
        if comp is False and previous_comp and self.comp_since is not None:
            self.cycles.append((t, t-self.comp_since))
            self.comp_since = None
        if comp is None:
            self.comp_since = None
        while self.starts and self.starts[0] < t-3600:
            self.starts.popleft()
        demand = bool(f['Y'] or f['Y2'] or f['W'] or f['W2'])
        if demand and self.demand_since is None:
            self.demand_since = t
            self.response_s = None
        if not demand:
            self.demand_since = None
        if demand and self.response_s is None and (comp or (mode == 'heating' and fan and f['supply_c'] is not None and f['return_c'] is not None and f['supply_c']-f['return_c'] > 3)):
            self.response_s = t-self.demand_since
        if f['ts'][:10] != self.day:
            self.day = f['ts'][:10]
            self.daily_runtime = 0
        if previous_comp:
            self.total_runtime += dt
            self.daily_runtime += dt
        if dt and f['power_w'] is not None and self.last_f['power_w'] is not None:
            self.energy_wh += (f['power_w']+self.last_f['power_w'])*.5*dt/3600
            self.energy_observed_s += dt
        f['split_c'] = None if f['supply_c'] is None or f['return_c'] is None else f['supply_c']-f['return_c']
        self.history.append((t, f, dt, previous_comp))
        while self.history and self.history[0][0] < t-3600:
            self.history.popleft()
        bases, zs, score = compare(self.baseline, f, mode) if steady else ({}, {}, None)
        def num(k): return f.get(k)
        def above(k, v): return num(k) is not None and num(k) > v
        def below(k, v): return num(k) is not None and num(k) < v
        def ratio(k):
            b = bases.get(k, {}).get('median')
            return num(k)/b if b is not None and b > 0 and num(k) is not None else None
        emitted = []
        enabled = set()
        def rule(key, condition, seconds, severity, confidence, explanation, causes, action, sensors, provenance='prototype_heuristic'):
            enabled.add(key)
            if not condition:
                self.conditions.pop(key, None)
                self.active.pop(key, None)
                return
            self.conditions.setdefault(key, t)
            if t-self.conditions[key] < seconds:
                return
            alert = dict(id=key, severity=severity, timestamp=f['ts'], confidence=confidence,
                         confidence_kind='heuristic evidence score; not calibrated probability',
                         explanation=explanation, possible_causes=causes, recommended_action=action,
                         evidence={k: f.get(k) for k in sensors}, threshold_source=provenance,
                         baseline_bucket=bases, anomaly_score=score)
            if key not in self.active:
                emitted.append(alert)
            self.active[key] = alert
        cooling = steady and mode == 'cooling' and comp is True
        split = f['split_c']
        rule('poor_cooling', cooling and split is not None and split > -5, 180, 'Warning', .55,
             'Cooling temperature response is weak after settling.', ['High latent load', 'Air mixing or sensor placement', 'Refrigerant or compressor performance'],
             'Check sensor placement and vents; arrange service if persistent.', ['split_c', 'indoor_rh', 'compressor_a'])
        rule('filter_restriction', steady and fan is True and ratio('filter_pa') is not None and ratio('filter_pa') > 1.4, 120, 'Warning', .72,
             'Filter pressure drop increased over 40% at matched conditions; airflow is not directly measured.', ['Dirty filter', 'Changed blower speed', 'Blocked or wet pressure tubing'],
             'Inspect filter and return vents; compare at the same blower setting.', ['filter_pa', 'blower_a', 'split_c'], 'learned_baseline + prototype_heuristic')
        rule('freeze_symptoms', cooling and below('suction_c', 0) and below('airflow_proxy', .65), 180, 'Probable fault', .78,
             'Cold suction surface and reduced airflow are consistent with icing.', ['Airflow restriction', 'Refrigerant-related fault', 'Surface probe error'],
             'Stop cooling using the normal thermostat and request HVAC service.', ['suction_c', 'airflow_proxy', 'split_c'])
        rule('blower_degradation', steady and fan is True and below('airflow_proxy', .65), 120, 'Probable fault', .70,
             'Blower electrical activity is present with a low calibrated airflow proxy.', ['Obstruction', 'Blower degradation', 'Proxy calibration error'],
             'Check accessible filter and vents; technician checks blower.', ['blower_a', 'airflow_proxy'])
        rule('condenser_fan_response', cooling and below('condenser_fan_a', .1) and above('outdoor_unit_c', 65), 60, 'Probable fault', .80,
             'Outdoor unit is hot with little measured condenser fan current.', ['Fan not running', 'Separate fan control strategy', 'Current sensor fault'],
             'Stop cooling at the thermostat and arrange service; avoid the outdoor electrical compartment.', ['compressor_a', 'condenser_fan_a', 'outdoor_unit_c'])
        rule('high_current', steady and ratio('compressor_a') is not None and ratio('compressor_a') > 1.3, 90, 'Warning', .65,
             'Compressor current is above its matched historical reference.', ['High load', 'Voltage issue', 'Mechanical or refrigerant problem'],
             'A technician should inspect current, voltage and operating conditions.', ['compressor_a', 'voltage_v'], 'learned_baseline + prototype_heuristic')
        rule('current_vibration', steady and ratio('compressor_a') is not None and ratio('compressor_a') > 1.3 and ratio('vibration_g') is not None and ratio('vibration_g') > 2, 90, 'Probable fault', .82,
             'Current and vibration increased together.', ['Mechanical stress', 'Loose mounting', 'Abnormal load'],
             'Arrange technician inspection; do not touch operating machinery.', ['compressor_a', 'vibration_g'], 'learned_baseline + prototype_heuristic')
        rule('no_response', bool(f['Y'] or f['Y2']) and comp is False and not f['defrost'], self.config.get('call_grace_s', 300), 'Probable fault', .80,
             'Cooling/compressor demand persists without measured compressor operation.', ['Protective delay or lockout', 'Power or contactor fault', 'Current sensor failure'],
             'Check normal thermostat status and request service if unresolved.', ['Y', 'Y2', 'compressor_a'])
        rule('blower_absent', bool(f['Y'] or f['Y2'] or f['W'] or f['W2'] or f['G']) and fan is False and not f['defrost'], 180, 'Probable fault', .75,
             'Demand persists without measured blower current after the response allowance.', ['Blower not operating', 'Protective delay or control strategy', 'Current sensor failure'],
             'Use normal controls to stop demand and have a technician check the blower.', ['Y','W','G','blower_a'])
        rule('heating_no_response', steady and bool(f['W'] or f['W2']) and split is not None and split < 3 and not f['defrost'], 420, 'Warning', .60,
             'A sustained heat call has not produced a positive air-temperature response.', ['Heating lockout', 'Stage or demand mapping error', 'Air mixing or sensor error'],
             'Check normal thermostat status and arrange service. Do not inspect combustion components yourself.', ['W','W2','split_c','blower_a'])
        rule('unexpected_operation', comp is True and all(f[k] is False for k in ('Y','Y2','W','W2')) and not f['defrost'], 300, 'Warning', .60,
             'Compressor activity persists without conventional thermostat demand.', ['Communicating controls', 'Unobserved demand', 'Stuck control'],
             'Verify control configuration before diagnosing equipment.', ['Y','W','compressor_a'])
        recent_cycles = [duration for end, duration in self.cycles if end >= t-3600]
        rule('short_cycling', len([x for x in recent_cycles if x < 180]) >= 3, 0, 'Warning', .75,
             'At least three completed compressor runs under three minutes occurred within one hour.', ['Sizing or control issue', 'Safety trips', 'Demand changes'],
             'Review cycle history and ask a technician to evaluate repeated cycling.', ['compressor_a'])
        rule('long_runtime', self.comp_since is not None and comp is True and t-self.comp_since > 7200, 0, 'Warning', .45,
             'Observed compressor run exceeded two hours.', ['Design-day load', 'Inverter operation', 'Reduced capacity'],
             'Compare weather and recovery; continuous runtime alone does not prove a fault.', ['outdoor_c','return_c','split_c'])
        past = next(((tt, ff) for tt, ff, _, _ in self.history if tt >= t-1800), (t, f))
        span = t-past[0]
        rh_rate = (f['indoor_rh']-past[1]['indoor_rh'])*3600/span if span >= 300 and f['indoor_rh'] is not None and past[1]['indoor_rh'] is not None else None
        recovery = (f['return_c']-past[1]['return_c'])*3600/span if span >= 300 and f['return_c'] is not None and past[1]['return_c'] is not None else None
        rule('humidity_degradation', cooling and above('indoor_rh', 65) and rh_rate is not None and rh_rate >= 0, 600, 'Warning', .55,
             'High indoor relative humidity is not declining during prolonged cooling.', ['Infiltration or moisture load', 'Oversizing', 'Poor latent performance'],
             'Check moisture sources and compare absolute humidity before service conclusions.', ['indoor_rh','return_c','supply_rh'])
        rule('condensate', f['condensate'] is True, 10, 'Critical', .95,
             'Independent condensate high-water sensor is active.', ['Blocked drain', 'Pump failure', 'Float or wiring issue'],
             'Use the normal thermostat to stop operation and inspect safely; keep factory overflow protection active.', ['condensate'])
        rise = self.config.get('verified_heating_rise_c')
        rule('heating_rise', steady and mode == 'heating' and rise is not None and split is not None and not rise[0] <= split <= rise[1], 120, 'Probable fault', .80,
             'Heating rise is outside the installation-specific verified range.', ['Airflow setting or restriction', 'Heating stage issue', 'Sensor placement'],
             'Have a technician compare the furnace nameplate rise and airflow.', ['split_c','blower_a'], self.config.get('heating_rise_source','installer_verified'))
        limit = self.config.get('verified_current_limit_a')
        rule('verified_overcurrent', steady and limit is not None and above('compressor_a', limit), 30, 'Critical', .85,
             'Current exceeds the configured technician-verified continuous limit.', ['Electrical or mechanical overload', 'Incorrect limit or sensor calibration'],
             'Stop operation using normal controls and contact a technician. This monitor is not overcurrent protection.', ['compressor_a'], self.config.get('current_limit_source','installer_verified'))
        rule('baseline_anomaly', steady and score is not None and score > .8, 180, 'Warning', .50,
             'One or more features deviate strongly from the matched baseline.', ['Changed load or equipment', 'Sensor drift', 'Developing fault'],
             'Review evidence and operating conditions; anomaly score does not identify a fault.', list(zs), 'learned_baseline')
        # Rules may become inapplicable; no old alert is allowed to imply fresh evidence.
        self.active = {k:v for k,v in self.active.items() if k in enabled}
        observed = sum(row[2] for row in self.history)
        observed_run = sum(row[2] for row in self.history if row[3])
        critical_missing = [k for k in ('return_c','supply_c') if f[k] is None]
        quality = 'incomplete' if critical_missing else 'limited' if comp is None or fan is None else 'good'
        severity = max((a['severity'] for a in self.active.values()), key=lambda x: SEVERITY[x], default='Normal')
        result = dict(timestamp=f['ts'], source=f['source'], mode=mode, compressor_state=comp, fan_state=fan,
                      health=severity, quality=quality, missing=critical_missing, steady=steady, values=f,
                      alerts=list(self.active.values()), new_alerts=emitted,
                      metrics=dict(anomaly_score=score, z_scores=zs, baseline_matched=bool(bases),
                        compressor_runtime_s=self.total_runtime, daily_runtime_s=self.daily_runtime,
                        duty_cycle=observed_run/observed if observed else None, observed_window_s=observed,
                        cycles_last_hour=len(self.starts), average_cycle_s=sum(recent_cycles)/len(recent_cycles) if recent_cycles else None,
                        response_s=self.response_s, energy_kwh=self.energy_wh/1000 if self.energy_observed_s else None,
                        energy_observed_s=self.energy_observed_s, humidity_change_pct_per_hour=rh_rate,
                        return_change_c_per_hour=recovery), gap_detected=gap)
        self.last_t, self.last_f, self.last_mode = t, f, mode
        return result
