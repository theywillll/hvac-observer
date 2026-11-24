# Threaded ADC worker bench probe

`current_worker_probe` exercises the pinned UNO Q blocking ADC reader from
a dedicated Zephyr thread. The sketch loop receives complete captures and
prints diagnostic JSON for negotiated sessions. It is a standalone bench sketch. The production
environmental sketch still reports pending current channels.

## Ownership and queue

The worker owns ADC1, the sequence reader, capture buffer and its diagnostic
clock. It initializes the reader once and waits for capture requests.
Each request reads 16 sweeps of A0/A1/A2
with a 1,000-us driver interval and a 100-ms sleep after each attempt. These
are requested settings. Actual conversion spacing and scheduling delay
require measurement.

The foreground dispatches tickets through a one-record `k_msgq` and
the worker returns copies through a two-record completion queue. Completion
puts wait for room, providing backpressure. Unread captures retain their
FIFO order. A full request queue stops the session and emits
`dispatch_failed`, preserving the queued request.
The foreground owns
Serial and computes per-channel mean and AC RMS in native ADC codes using
the existing accumulator. The clock brackets the whole reader call.
Its nominal 100-us resolution supplies request diagnostics. Per-channel
aperture timestamps, synchronized continuous windows and calibrated current
still require additional work.

The thread uses a static 4,096-byte stack and priority 7. The queue is
initialized explicitly before the thread is created. Stack margin, scheduling
interference and latency remain hardware measurements.

The pinned 1.0.0 headers, configuration and symbol map expose the required
thread, queue and sleep APIs. Current Zephyr documentation describes
[message queue copying](https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html)
and [thread lifecycle](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html).
The actual pinned-toolchain compile is the compatibility check.

## Diagnostic records

The first line identifies `hvac_adc_worker`, schema version 2, the native
14-bit range, ADC channel order, sweep count and both queue depths. A subsequent
`setup_status` record reports the driver's setup result. A setup error
ends the worker.

Capture records include these fields.

| Field | Meaning |
| --- | --- |
| `request` | Zero-based ADC attempt ordinal across the worker lifetime |
| `session` | Decimal string identifying the dispatch session |
| `capture_ordinal` | Decimal string identifying the capture within its session |
| `accepted` | Foreground accepted this completion's ticket and clock checks |
| `clock_ok` | Both local clock reads succeeded |
| `started_us`, `returned_us` | Boot-relative reader-call bracket |
| `driver_status`, `guard_ok` | Original ADC return and buffer guard check |
| `channels` | Three ordered channel statistics with faults, sample and near-rail counts |

The channel statistics are finite-block mathematical quantities. A
16-sample block at the requested interval is insufficient to establish a
50/60-Hz motor-current RMS measurement. The probe reports ADC codes.
Rejected completions, failed, incomplete or out-of-range sequences suppress
all channel statistics.
A near-rail observation suppresses that channel's numeric statistics while
retaining its fault and count. A constant, usable block has zero AC RMS.
The synthetic tests exercise these cases with known channel patterns.

Clock failure, guard damage or request-counter exhaustion ends ADC activity.
A final `terminal_reason` record uses 1 for clock failure, 2 for guard damage
and 3 for counter exhaustion. Setup and terminal
records wait for a queue slot so the foreground can observe them. A stalled
consumer can delay those records indefinitely. The driver read itself also
has no application cancellation timeout.

## Session owner and Bridge

The sketch loop and safe Bridge callbacks own the session store and its
clock. The worker receives ticket copies and keeps its own diagnostic clock.
The owner calls `beginCapture` before queuing a request and `finishCapture`
when a completion arrives. The ADC read runs outside the owner's context.
Expiry or a new offer can invalidate its ticket while that read is running.
The foreground then prints `accepted:false` and null statistics.

The probe provides the same `hvac_current_boot` and `hvac_current_session`
commands described in [current-session-contract.md](current-session-contract.md).
Wait for `setup_status:0` before negotiating. Discover the token, reserve a
fresh host session, send its offer and confirmation, then renew it within
six seconds. Capture dispatch starts after confirmation. The diagnostic policy
uses the native 14-bit range and 16-sweep size with zero calibration IDs.
The probe reports records on Serial and provides these two handshake endpoints.
The host service uses the environmental sketch's snapshot endpoints.

Clock or terminal reader failures stop the session. A new offer stops
the previous session and the foreground rejects its remaining completions.
Reconfiguration leaves the ADC owner running so it can drain old work and
accept a later request. Blocking reads can finish after expiry. Their
completions still pass through the session check.

## Build and manual bench checks

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --output-dir build/worker-probe firmware/current_worker_probe
```

This increment compiled the sketch and ran synthetic software tests. The
following board checks remain pending. Use the protected SELV fixtures and
input review described in [sequence-probe.md](sequence-probe.md).

1. Capture the startup and setup records under manual supervision, negotiate a
   session and keep its lease renewed. Apply known
   safe DC levels to each channel and check channel identity and mean codes.
2. Record reader duration, task scheduling and channel aperture behavior with
   a suitable reference instrument. Check the thread stack margin.
3. Set `CONSUMER_PAUSE_MS` to 250 for a deliberate slow-consumer run. Confirm
   backpressure, FIFO order and recovery after normal consumption resumes.
   Stop renewals or offer a new session while a read is outstanding and check
   that old completions report `accepted:false`. Keep resets in separate captures.
4. Exercise setup and reader failures through reviewed fixtures. Verify
   failure records and latched termination without automatically restarting
   the worker.

The queue now carries session tickets through dispatch and completion in
the diagnostic probe. Physical timing and reset checks remain pending.
The environmental sketch now has an opt-in path using the same worker.
A worker result will need
qualified per-channel timing and calibration before producing amperes.

## Environmental sketch bench build

The default build leaves the ADC worker disabled. For a reviewed SELV bench
setup, compile its integration path with this option.

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --build-property compiler.cpp.extra_flags="-DHVAC_ENABLE_CURRENT_WORKER=1" --output-dir build/environmental-worker firmware/hvac_observer
```

The worker initializes in its own thread. The sketch owner waits for its
setup result before accepting session commands, then dispatches captures
only for a confirmed session. It drains completions outside the I2C reads
and rejects expired or replaced tickets. The shared implementation is
`UnoQCaptureWorker`. Each instance requires global boot-lifetime storage.

An accepted capture updates the combined frame's fault metadata through
`finishUnqualifiedCapture`. Channels retain null current, with timing and
calibration faults. Source/rail failures also set the feature fault for
the affected channels. Existing sample/window metadata is preserved when
revoking an earlier value. New sample endpoints and window IDs require
qualified timing evidence. Faults latch until a fresh qualified publication
or a new session.

The opt-in policy uses 16 samples and the native 14-bit range. Its requested
sampling settings are still diagnostic. Captures, setup failures, backpressure,
expiry and responsiveness during environmental I2C reads require board testing.
CI compiles both environmental variants and the diagnostic probe.
