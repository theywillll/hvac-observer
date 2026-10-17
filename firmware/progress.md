# Current-sensing development record

## 2026-10-17 — Stock-core multi-channel sequence bench probe

Remote baseline inspected: `fc456963e758437c21ed9d5799912283a1ccf722`.
Added a standalone blocking Zephyr ADC probe for 16 sweeps of channels 9/10/11
on ADC1, with native 14-bit output. Pin configuration and initialization errors
fail closed. Failed/delayed requests, damaged guards and incomplete fills never
publish code arrays. Guard damage stops further requests until manual reset.
The JSON-lines analyzer checks one complete session and demultiplexes by ADC ID.
It reports request timing only, leaving actual sample timing unknown.

Validation: 43 Python tests passed. Native C++11 tests with undefined-behavior
checks passed. The direct probe compiled and linked on the stock UNO Q
CLI 1.5.1 / core 1.0.0 target. Final sizes and source/binary hashes are in
[sequence-build-record.json](sequence-build-record.json). CI is configured to
compile all four sketches and run native address/undefined-behavior checks;
remote results belong to the commit's workflow runs, not this local claim.

Sources: pinned ADC and pin-control implementations, overlay and installed ADC
headers. See [probe design and manual bench procedure](sequence-probe.md).
Publication is traceable in the [commit history](https://github.com/theywillll/hvac-observer/commits/main).

Limitations: no hardware upload or capture, no proven sample cadence/aperture,
no synchronized worker, RMS/calibration or current-frame integration. The
existing block pool is deliberately not fed invented per-sample timestamps.
Protected fixtures, a board and reference instruments are needed to qualify
actual channel order, interval behavior, blocking latency and accuracy.

Next increment: define a bounded worker/session interface with explicit timing
provenance and synchronization, or develop raw-code feature calculations behind
strict validity gates while physical timing qualification remains pending.

## 2026-10-15 — Bounded raw-block ownership and timing contract

Write access restored. Remote baseline checked:
`dc15fcf5dfa473d9080feec9d7dc47a87b042774`; existing local histories preserved.

Added a fixed-capacity three-channel block pool with explicit fill/ready/read
ownership, FIFO leases, stale-lease rejection and reset invalidation. Unread
blocks are never overwritten; full pools report overruns. Each channel retains
its own timing bounds. Invalid codes, ADC errors, near-rail observations,
overlapping/backward calls and incomplete/aborted blocks remain invalid.
All access must be serialized externally; this is not a concurrent DMA queue.

Validation: 34 Python tests pass. Shared C++ synthetic tests pass on Windows
using Zig 0.13.0 as a C++11 compiler with undefined-behavior checks, including
1,000 queue reuse cycles. The same test logic compiles for the pinned UNO Q
target in `current_block_selftest`, without uploading. Final sketch report:
99,396 bytes storage / 48,272 bytes globals. Native pool size is 9,888 bytes for
two 800-sample/channel slots. See [the build record](block-build-record.json).
CI now runs native address/undefined-behavior checks and compiles this third
sketch. Passing local checks does not claim that remote CI has already run.

The pinned headers/configuration were reviewed again: blocking device-dispatch
ADC API exists, interval precision depends on the driver, and async ADC remains
disabled. A direct Zephyr sequence backend has not been compiled or selected.
Details and reproduction: [raw block contract](block-contract.md).
Publication is recorded in the [commit history](https://github.com/theywillll/hvac-observer/commits/main).

Remaining: no board capture, deterministic scheduler, synchronized acquisition
worker, RMS/calibration or current-frame integration. No HVAC controls added.
Next increment: qualify the stock-core blocking multi-channel sequence API and
its pin/configuration ordering, then define worker synchronization and explicit
timing-failure handling. Hardware timing and calibration remain separate gates.

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
