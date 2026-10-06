# Local API and acquisition contract

The server binds **127.0.0.1 only**, port 8765 by default. It has no internet-facing authentication layer and no write API. All values use SI-like engineering units named in fields; time is normalized to UTC ISO 8601.

| Method and path | Response |
|---|---|
| GET `/api/status` | `latest`, `stale`, `age_s`, `error`, `simulation`, `scenario` |
| GET `/api/history?limit=720` | Current session result list, chronological; limit clamped to 1…2000 |
| GET `/api/profiles` | Profile catalog with parameter citations and explicit unknowns |
| GET `/api/profile?manufacturer=...&model=...` | Case-insensitive exact identity match, or null |
| GET `/` | Dashboard; `/app.js` and `/style.css` served locally |
| POST anywhere | 405; no equipment control/remote ingestion endpoint |

Unknown paths return 404; invalid history limits return 400. The acquisition adapter calls the service in-process. SQLite writes and API reads use one application lock. JSON output rejects NaN/infinity. Unhandled acquisition exceptions stop the stream and show an error; last good data is marked stale after 15 wall-clock seconds.

Example input frame (other sensors omitted = null):

```json
{"ts":"2026-10-06T06:00:00Z","seq":12,"source":"hardware","stage":1,"return_c":25.0,"supply_c":15.0,"indoor_rh":52.0,"outdoor_c":32.0,"compressor_a":9.1,"blower_a":2.0,"filter_pa":42.0,"Y":true,"Y2":false,"W":false,"W2":false,"G":true,"OB":false,"R":true,"condensate":false,"defrost":false}
```

`seq` must increase during a session; restart service after device reset. `source` is `hardware`, `simulator` or `replay`; simulation is selected explicitly at service startup. Temperature fields are °C, currents A RMS, pressure Pa, power real W, voltage V RMS, vibration dynamic g RMS, airflow proxy dimensionless with 1.0 representing the commissioned reference. Thermostat fields are bool/null. Stage is integer 1 or 2. Unknown keys and booleans masquerading as numbers are rejected.

`latest` contains state, quality, measured values, active alerts, newly emitted alert events, feature metrics and gap flag. Each alert contains severity, timestamp, rule ID, heuristic confidence, evidence, explanation, possible causes, recommended next action, threshold provenance and anomaly score. Anomaly score is a normalized deviation (0…1), not fault probability.

`/api/history` is intentionally scoped to the active session, even though SQLite retains earlier sessions. Alert event rows log activations, while state payloads preserve the active alert set at each time. Acknowledgement/escalation/outbound messaging, user accounts and multi-installation routing are not implemented.
