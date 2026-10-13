# Current-sensing development record

## 2026-10-13 — Build target and raw ADC characterization tools

First increment: established `arduino:zephyr:unoq` on CLI 1.5.1 / core 1.0.0,
compiled the existing environmental sketch, added a separate slow A0-A2 raw-code
probe and a capture analyzer with eight synthetic regression tests. Added a
pinned CI compile job, source-backed acquisition design and a bench procedure.

Local validation: 34 Python tests passed. Both sketches compiled for the actual
UNO Q target on Windows. The environmental build reported 94,756 bytes of sketch
storage and 39,548 bytes of globals; the probe's final size and binary/config
hashes are in [build-record.json](build-record.json). Upstream Wire compilation
emits missing-field-initializer warnings with `--warnings all`. This is not a
warning-free build or a hardware test.

Remote baseline inspected: `619a800d5789c6decead66b8ad5272917d9488e6`.
Original local `main` history is preserved. Work starts on `github-main` from the
remote commit after confirming the tracked file trees matched; no history is
rewritten. This increment's publication is identifiable in the
[repository commit history](https://github.com/theywillll/hvac-observer/commits/main).

Remaining: no board, reference instrument or real capture; no deterministic
sampling, buffering, RMS/current calibration or current-frame integration.
The stock release has no `CONFIG_ADC_ASYNC`; ADC/DMA backend suitability and
8-ksample/s-per-channel throughput are unproven.

Next increment: compile and test the bounded acquisition/block contract and
verify a stock-core backend's available APIs before selecting a timing strategy.
Keep this separate from the I2C loop. Do not enable calibrated current readings
until validity, timing and calibration are established.
