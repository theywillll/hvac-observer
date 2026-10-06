# Setup and commissioning

## 1. Simulation first

Use Python 3.11+. Run commands from the repository root:

```sh
python -m scripts.init_db hvac.sqlite3
python -m backend.server --scenario dirty_filter --speed 20
```

Browse to http://127.0.0.1:8765. Restart with a different `--scenario` to compare behavior. A new session keeps old database rows but shows the new session's history. Export synthetic data:

```sh
python -m scripts.simulate --scenario frozen_evaporator --count 900 --output frozen.jsonl
python -m unittest discover -s tests -v
```

## 2. Bench hardware

Wire only the SELV sensor side. Check sensor address, supply and pullup voltage with a meter. Compare both temperature probes together in stable room air before installing; document offset, date, reference and uncertainty. Do not use boiling water to check a humidity breakout. Equalize pressure ports and record zero. Test sensor disconnection, reversed pressure tubes, CRC corruption, cable disturbance and MCU reset.

Use the [firmware instructions](../firmware/README.md). Isolated thermostat channels and float are off by default. Test the input board using a protected isolated low-voltage AC test supply; verify no false activation at zero and valid recognition across the design voltage/temperature range. No live HVAC connection yet.

## 3. Configure the installation

Copy `config.example.json` to `config.local.json`. Record manufacturer and exact indoor/outdoor model numbers, equipment type and mode/stage configuration. Allowed implemented mode configuration: `split_ac`, `heat_pump` or `unknown`; gas/electric heating is detected through W/W2. The profile catalog describes broader types, but thermostat decoding still needs this control configuration.

Known equipment: use `/api/profile?manufacturer=Carrier&model=24SCA5` to retrieve a source-backed reference. Family match does not authorize limits. Verify exact nameplate/manual, installed coil match, blower setting and control version before setting `verified_heating_rise_c` or `verified_current_limit_a`. Include `heating_rise_source` / `current_limit_source` as a document URL and page or installer record. The dashboard profile browser is reference-only; selection does not silently alter rules.

Unknown equipment: enter the broad equipment/control type, supply voltage from the label if known, heat-pump O/B polarity, available sensors and known stages. The system does not autonomously recognize an equipment model. Keep unverified limits null. Baseline-free generic checks provide limited evidence while learning.

Example calibration entry (illustrative, not supplied calibration):

```json
{"calibration":{"return_c":{"gain":1.0,"offset":-0.2,"date":"2026-10-06","reference":"bench reference serial ..."}}}
```

The service stores original engineering-unit readings and calibrated values separately. ADC counts, waveform samples and device calibration coefficients should be archived by the external high-rate acquisition node.

## 4. Professional installation

A qualified installer places duct taps/probes and any field-wiring or current/power sensors. Ensure each added connection is fused/isolated as appropriate, approved for its environment and does not change factory safety operation. Confirm safe cable routes and remove all temporary bench connections. Record photos, equipment identity, firmware/core versions and calibration in a commissioning log.

For a thermal-only setup use `--source uno` or `--source pi`. Pi requires enabled I2C and `smbus2==0.5.0`. Data from an expanded instrument gateway can be written as fresh newline-delimited JSON to an append-only local spool, read with `--source jsonl --input readings.jsonl`. Stale spool records cause a visible stop; rotate/start an empty spool for a new acquisition session.

## 5. Reviewed learning

Capture normalized input frames from the acquisition process for at least seven days; recommended two to four weeks. The SQLite export command below reconstructs calibrated input fields from a chosen session:

```sh
python -m scripts.export_session hvac.sqlite3 commissioning.jsonl
python -m scripts.commission commissioning.jsonl models/installation.json --config config.local.json --reviewed-healthy
python -m backend.server --source uno --config config.local.json --baseline models/installation.json
```

The export defaults to latest session. A seven-day run is required for hardware training; interrupted short sessions need an explicit, reviewed merge with fresh monotonic sequence numbers. Do not use `--allow-synthetic` to bypass field commissioning. Synthetic baseline models are rejected in hardware mode.

## 6. Maintain and operate

Keep the service local. From another device, use an authenticated SSH tunnel: `ssh -L 8765:127.0.0.1:8765 user@device`. Open localhost:8765 on that device. Do not expose this development HTTP server to the internet. No outbound alerts are sent; local alerting is implemented in the dashboard/database only.

On Linux, adapt `scripts/hvac-observer.service` to the installed user/path. Grant only needed I2C device access, not unrestricted root. Store data on durable media; size retention empirically because the prototype stores both normalized observations and per-frame JSON. Back up SQLite with its backup API, not an uncoordinated copy of a running WAL database. Use the explicit `scripts.prune` cutoff only after a backup. A production upgrade should downsample old data and test disk-full handling.

After replacing filters, changing blower settings or servicing equipment, annotate and review the reference baseline. Never overwrite the old model without preserving its version and learning period. Verify each sensor periodically, and review unknown/stale status as a monitoring outage, not evidence of healthy HVAC operation.
