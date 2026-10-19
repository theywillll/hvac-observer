# Current-sensing development record

## 2026-10-19 — Window qualification and calibration contract

Remote baseline inspected: `534bb39a5201c884aa8134460f462996c370f38f`.
Added a per-channel gate for strictly increasing sessions, ordered windows,
gaps/duplicates, freshness, qualified aperture timing and expected ADC code
scale. Raw features now retain their code maximum. Unsupported/unqualified or
unapproved calibration records cannot enable conversion. The policy pins an
active record/revision per channel; a new session is required to change it.

Schema 1 conditionally multiplies raw-code RMS by a finite measured-chain gain
only inside its calibrated range. Every failure returns an unavailable numeric
result. Old windows are not resurrected after failure or reset. All calibration
records, evidence IDs and timestamps in the tests are synthetic fixtures.

Validation: shared native C++11 tests passed with undefined-behavior checks;
the existing 43 Python tests passed. The expanded self-test compiled on pinned
UNO Q CLI 1.5.1 / core 1.0.0. Final build sizes/hashes and boundaries are in
[window-build-record.json](window-build-record.json). CI runs native address/
undefined-behavior tests and compiles all four sketches; consult commit workflow
runs for their remote conclusions. Publication is traceable in the
[commit history](https://github.com/theywillll/hvac-observer/commits/main).

No real calibration or timing record exists. The gate is not wired to probes,
the environmental sketch or production frames. No board upload, physical
measurement, hardware control or validated current reading occurred.
The existing request-bracket probes deliberately cannot pass the timing gate.
See [contract, state semantics and bench requirements](current-window.md).

Next increment: bounded session/worker orchestration and nullable quality-frame
integration without asserting unmeasured aperture timing. Actual cadence/skew,
calibration coefficients and accuracy remain dependencies on a physical UNO Q,
reviewed SELV fixtures and reference instruments.

## 2026-10-17 — Raw-code offset removal and RMS math

Remote baseline inspected: `d8b599a07c239a4b22e49da40432fa30e43396d1`.
Added a bounded, independent-per-channel/window accumulator for estimated DC
offset and AC RMS in ADC codes. Exact 64-bit integer centering prevents loss of
small ripple under a large DC offset. Configuration/source/length/code/rail/
overflow faults leave numeric features unavailable rather than returning zero
or old-window values. Reset clears every prior sum and fault.

Validation: shared synthetic C++11 tests passed natively with undefined-behavior
checks. They include a rounded sine, DC/offset invariance, independent channels,
near-rail/error gates, reset, 100 varied reference comparisons and the full
65,535-sample arithmetic boundary. The expanded UNO Q self-test compiled with
CLI 1.5.1 / core 1.0.0, reporting 104,228 bytes storage / 51,488 bytes globals.
The existing 43 Python tests passed. See [feature-build-record.json](feature-build-record.json).
CI runs the shared tests under address/undefined-behavior checks and compiles all
four sketches. Remote conclusions belong to the commit's workflow runs.

Only the synthetic self-test invokes these calculations. No ADC backend,
environmental sketch or production frame uses them yet. There is no amperes
conversion, physical timing/accuracy evidence, board upload or hardware control.
The estimated sample mean is not a measured bias voltage; zero raw AC RMS does
not prove zero motor current. See [math and validity contract](raw-features.md).
Publication is traceable in the [commit history](https://github.com/theywillll/hvac-observer/commits/main).

Next increment: explicit timing/session/window-age qualification and versioned
calibration contracts before frame integration. Hardware measurements are still
required to select a trustworthy acquisition cadence and establish coefficients.

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
