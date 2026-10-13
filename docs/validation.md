# Validation record

Last Python software check: 2026-10-13 on Windows: 34 tests passed, including eight synthetic raw-ADC capture-analysis tests. The earlier 26-test suite also passed in GitHub Actions on Python 3.12. The original transcript is in `test-results.txt`; `data/scenario_results.json` records first-alert times and outputs for the synthetic scenarios. Firmware compilation is recorded separately below.

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

No UNO Q or Pi was connected during development. The environmental UNO Q sketch and separate `current_adc_probe` compiled on 2026-10-13 using CLI 1.5.1, core 1.0.0 and FQBN `arduino:zephyr:unoq`; neither was uploaded. See [build versions and hashes](../firmware/build-record.json) and [the acquisition design](../firmware/current-acquisition.md). The probe's capture-analysis tests use synthetic CSV. Sensor accuracy, timing, isolation, electrical behavior, EMC and real HVAC diagnoses haven't been tested. The SVG drawings aren't PCB fabrication files.

Before field use:

1. Reproduce the pinned build and verify compatibility with the actual board's core/loader before any manual upload. The initial compile and hashes are recorded, but no board compatibility test has been performed.
2. Bench-test each sensor on a protected low-voltage supply, including wiring faults, I2C timeouts, resets and 50/60-Hz demand inputs.
3. Validate the isolated input assembly, including its load on the HVAC transformer and behavior with controller leakage.
4. If adding current, vibration or power acquisition, check timing, anti-aliasing, clipping and accuracy against reference instruments.
5. Have a qualified technician commission the installation and record the exact nameplate limits.
6. Collect independently reviewed healthy and fault data across seasons and stages. Measure false alerts, detection delay and confidence calibration before wider use.

## Open work

The initial compilation milestone is complete. Next are a repeatable sensor bench test and a separately qualified waveform acquisition backend. Expanded acquisition drivers, watchdog/recovery tests and a qualified enclosure/input assembly are still needed.

For longer deployments, storage needs disk-full and long-duration tests, multi-session aggregation and tested migrations. Automatic baseline adaptation, alert acknowledgement, outbound notifications, authenticated multi-user access and signed updates aren't implemented.
