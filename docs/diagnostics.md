# Diagnostic rules

## Interpretation

`ΔT = T_supply − T_return`. Cooling normally gives a negative difference; heating gives positive rise. The software never mixes the sign conventions. Relative humidity depends on temperature: a higher supply RH does not by itself mean moisture was added.

Severity is operational urgency: Normal / Warning / Probable fault / Critical. Unknown, stale and incomplete are **data-quality states**, not a fifth fault severity or a Healthy result. No sensor coverage means no conclusion. Alerts have a bounded 0–1 heuristic evidence score; an 82% score is **not** a statistically calibrated 82% chance that a specific component failed.

Every numeric threshold below is an **engineering prototype heuristic** unless explicitly marked learned or installer-verified. They are configurable starting hypotheses for commissioning, not manufacturer guarantees. DOE discusses airflow and refrigerant diagnostics as interdependent; simple surface/air observations cannot prove charge. [DOE diagnostic guide](https://www1.eere.energy.gov/buildings/publications/pdfs/building_america/measure_guide_air_cond_diagnostics.pdf)

## Implemented deterministic rules

Rule persistence is measured in elapsed sample time, not number of frames. A gap >15 s resets persistence. General thermal settling is 180 s after a mode/stage change. Active alerts clear when their evidence condition becomes false; event history remains in SQLite. No explicit hysteresis is implemented beyond time persistence: commissioning must assess chatter near boundaries.

| Rule / sensors | Formula and threshold | Persistence | Explanation / causes | Evidence score; false positives |
|---|---|---|---|---|
| Weak cooling: supply, return, compressor | ΔT > −5°C during settled cooling with measured compressor on | 180 s after settling | Weak sensible cooling; latent load, mixing, refrigerant or compressor symptom | .55; startup, humidity, sensor placement, variable capacity |
| Filter: filter Pa, blower, baseline | DP/DP_baseline >1.40 in matched context | 120 s settled | Increased restriction; filter, changed speed, tubing issue | .72; filter replacement/type, zoning, wet taps; not enough to separate filter vs flow by itself |
| Icing symptoms: suction surface, calibrated airflow proxy, compressor | suction <0°C AND airflow relative <0.65 | 180 s settled cooling | Consistent with icing; low flow or refrigerant-related conditions | .78; poor contact, inaccurate flow proxy, transients |
| Blower degradation: motor A, calibrated airflow proxy | blower on AND airflow relative <0.65 | 120 s settled | Electrical operation without expected air movement | .70; commanded low-speed/zoning, bad proxy |
| Condenser fan: fan A, compressor A, casing T | fan <0.1 A AND casing >65°C while compressor cooling | 60 s settled | Fan response absent with heat accumulation | .80; variable fan strategy, probe solar heating, CT below range |
| Compressor current: A, baseline | I/I_baseline >1.30 | 90 s settled | Above historical same-context load | .65; voltage and speed changes, dirty outdoor coil, load |
| Correlated mechanical symptoms: current + vibration baselines | current ratio >1.30 AND vibration ratio >2 | 90 s settled | Correlated electrical/mechanical change | .82; changed accelerometer mounting, external vibration |
| Demand without compressor: Y/Y2 + current | call true AND measured current below on threshold | 300 s | Lockout, delay, power/contactor issue, bad CT | .80; installer must account for longer manufacturer delays |
| Demand without blower: Y/Y2/W/W2/G + blower current | demand true AND blower explicitly measured off | 180 s | Blower, power, control or sensor issue | .75; heating fan delay and special control strategies |
| Heating without thermal response: W/W2 + temperatures | ΔT <3°C during settled heat call | 420 s after settling | Heating lockout, stage mapping or sensor problem | .60; not a combustion diagnosis; defrost suppressed |
| No call but compressor on | current on AND Y,Y2,W,W2 all explicitly false | 300 s | Control mismatch or stuck control | .60; communicating systems, post-run, missing call channel; unknown input never equals false |
| Short cycling: compressor current transitions | ≥3 completed on-cycles each <180 s in preceding hour | Event count | Repeated cycling | .75; intentional demand changes, staged/inverter operation; first observed running sample is not a start |
| Long running: compressor transitions | witnessed continuous run >7200 s | Immediate at threshold | Long cycle | .45; normal design-day/inverter operation; not proof of inefficiency |
| Humidity: RH and cooling context | indoor RH >65% AND observed RH slope ≥0 | 600 s after slope history available | Moisture load or latent-performance symptom | .55; infiltration, changing return temperature, people/activity |
| Drain: independent contact | high-water true | 10 s | Blockage/pump/float issue | .95; wiring damage can mask NO contact; critical is urgency, not certain drain diagnosis |
| Heating rise: supply/return + verified limit | ΔT outside exact installation's configured [min,max] | 120 s settled heating | Airflow or heater stage mismatch | .80; disabled unless limit supplied; verify nameplate and source |
| Excess current: current + verified continuous limit | I > explicitly configured limit | 30 s settled | Electrical or mechanical overload symptom | .85; critical; not breaker replacement. **MCA, MOCP, RLA, LRA are not interchangeable limits.** |
| Baseline anomaly: available features | max robust z / 6 >0.8 | 180 s settled | Unusual multichannel behavior | .50; mode/season not represented, sensor drift; not a diagnosis |

Input validity bounds are sensor plausibility checks, not HVAC alarm limits. Invalid frames stop acquisition visibly. More granular per-channel rejection with a sensor-fault event would be a useful field upgrade. Missing values are allowed and suppress rules requiring them.

## Baselines

Collect at least seven days (two to four weeks preferred) of technician-reviewed healthy data. `scripts.commission` excludes startup, unknown mode and active rule alerts; human review is still required because undetected faults can contaminate a baseline. It never silently trains on field anomalies.

Partition by mode, stage, 5°C outdoor bin, 3°C return bin and 10%RH indoor bin. Store per-feature median, robust scale `max(1.4826*MAD, noise_floor)` and sample count. Require at least 30 eligible samples per feature/bin. Thirty samples are a numerical minimum, not evidence of diverse independent cycles. Review cycle count and weather coverage before trusting a bucket. If a context is absent, return `null` anomaly score instead of extrapolating.

Feature floors: temperature split 0.6°C, filter 3 Pa, compressor 0.5 A, blower 0.2 A, vibration 0.02 g, real power 100 W, airflow proxy 0.05. These avoid division by near-zero noise and are prototype choices. Baselines remain frozen until explicitly reviewed and replaced; this prevents an adaptive filter from normalizing progressive failure. Historical charts permit trend inspection. Automatic seasonal adaptation and statistically validated degradation-rate estimation are future work.

No blower-speed measurement is available in the base build. Stage/context bins only partially address variable-speed confounding. For ECM/inverter/mini-splits, add actual command/speed telemetry before trusting current/pressure ratios.

## Candidate algorithms

| Method | Strength | Limitation / decision |
|---|---|---|
| Rolling z-score | Small, understandable | Outliers contaminate mean/variance; use reviewed median/MAD reference first |
| EWMA | Smooth gradual drift, O(1) state | May absorb degradation if its reference adapts; future residual-trend display |
| Isolation Forest | Lightweight multivariate novelty | Needs representative training and held-out threshold calibration; optional next step |
| One-Class SVM | Flexible boundary | Scaling/kernel/nu sensitivity; less attractive for low-data commissioning |
| Local Outlier Factor | Local density structure | Novelty mode and stable training set needed; sparse operating regimes problematic |
| Autoencoder | Captures richer nonlinear patterns | Data and validation cost not justified initially |
| Change-point detection | Highlights step changes | Mode, maintenance and weather changes must be segmented first |

**Initial implementation:** reviewed robust baseline residuals plus deterministic engineering rules. These algorithm comparisons are design judgments, not a measured benchmark. Model files are JSON, never executable pickle files from untrusted sources.

## Derived metrics

Compressor runtime is time-integrated measured on-state over observed intervals. Duty = observed running seconds / observed window seconds (up to one hour). Cycles/hour counts witnessed starts in that window; mean duration uses completed cycles. Gaps are excluded. Daily runtime resets on UTC date. Service restart begins a new session; totals are session-specific, not lifetime totals. Full cross-session reporting is an extension.

Energy = trapezoidal integral of **real measured W** / 3,600,000 for kWh. No power factor is invented from CT current; apparent power is not billed real energy. Energy coverage seconds are exposed. Daily runtime is not multiplied by a nameplate current to fabricate kWh.

Response time = elapsed demand time to observed compressor operation (or for heating, blower plus positive temperature response). A call already active when recording starts is left-censored; reported time is only since observation. Humidity and return-temperature rates use available observations up to 30 minutes; the current prototype does not fit a load model or normalize them to absolute humidity. Runtime, current, pressure, vibration and anomaly history can be charted.

Relative performance/airflow degradation require comparing like conditions and a commissioned airflow proxy. An approximate `Q_sensible ≈ rho * cp * volumetric_flow * abs(ΔT)` only becomes meaningful when airflow and density are actually measured. Total cooling requires humidity ratio/enthalpy and dry-air mass flow. Exact COP/SEER or refrigerant charge is outside the available measurements.
