# Validation record

Last software check: 2026-10-06, using Python 3.12.5 on Windows and Node 24.19.0. The 26-test suite also passed in GitHub Actions on Python 3.12. The local transcript is in `test-results.txt`; `data/scenario_results.json` records first-alert times and outputs for the synthetic scenarios.

## Software

Run the suite with:

```sh
python -m unittest discover -s tests -v
```

Tests cover fault detections and healthy heating/cooling runs, missing-current suppression, startup delays, data gaps, duplicate sequence numbers and timestamps, heat-pump O/B polarity, defrost and stage changes. They also cover UTC normalization, energy integration, null power, sensor CRC errors, calibration, waveform RMS/clipping, baseline context matching, citation integrity, SQLite persistence, HTTP endpoints and stale readings.

The scenarios use constructed readings for normal cooling, normal heating, dirty filters, frozen-evaporator symptoms, short cycling, failed-condenser-fan symptoms, blower degradation, compressor current/vibration anomalies, no compressor response, humidity degradation and high condensate.

Healthy scenarios should produce no alerts. Fault scenarios check the rules against known input patterns. They don't measure field precision or sensitivity. Some patterns trigger several related alerts because the readings alone can't identify a single cause.

## Dashboard

HTTP endpoint and asset delivery tests passed, as did `node --check dashboard/app.js`. Visual browser testing is still incomplete: the test browser couldn't reach the temporary server, and local file navigation was blocked.

`dashboard-preview.html` contains the dashboard code and a fixed synthetic dirty-filter fixture. Open it locally to inspect the layout and chart/profile selectors. For live behavior, use the Python service.

## Hardware

The KiCad 10.0.6 schematic project in `hardware/kicad/` loads and exports successfully. Electrical rules checking reports zero errors and warnings; 108 additional netlist checks passed. The four rendered sheets were visually reviewed. These checks cover the drawing and connectivity, not electrical simulation or physical hardware behavior.

No UNO Q or Pi was connected during development. The UNO Q sketch was reviewed against the documented sensor protocols, but hasn't been compiled or uploaded. Sensor accuracy, isolation, electrical behavior, EMC and real HVAC diagnoses haven't been tested. The SVG drawings aren't PCB fabrication files.

Before field use:

1. Compile the sketch with the selected UNO Q core and record the versions and binary hash.
2. Bench-test each sensor on a protected low-voltage supply, including wiring faults, I2C timeouts, resets and 50/60-Hz demand inputs.
3. Validate the isolated input assembly, including its load on the HVAC transformer and behavior with controller leakage.
4. If adding current, vibration or power acquisition, check timing, anti-aliasing, clipping and accuracy against reference instruments.
5. Have a qualified technician commission the installation and record the exact nameplate limits.
6. Collect independently reviewed healthy and fault data across seasons and stages. Measure false alerts, detection delay and confidence calibration before wider use.

## Open work

The next hardware milestone is a compiled sketch and repeatable sensor bench test. Expanded acquisition drivers, watchdog/recovery tests and a qualified enclosure/input assembly are still needed.

For longer deployments, storage needs disk-full and long-duration tests, multi-session aggregation and tested migrations. Automatic baseline adaptation, alert acknowledgement, outbound notifications, authenticated multi-user access and signed updates aren't implemented.
