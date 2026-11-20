# Threaded ADC worker bench probe

`current_worker_probe` exercises the pinned UNO Q blocking ADC reader from
a dedicated Zephyr thread. The sketch loop receives complete captures and
prints diagnostic JSON. It is a standalone bench sketch. The production
environmental sketch still reports pending current channels.

## Ownership and queue

The worker owns ADC1, the sequence reader, capture buffer and boot-relative
clock. It initializes the reader once, then requests 16 sweeps of A0/A1/A2
with a 1,000-us driver interval and a 100-ms sleep after each attempt. These
are requested settings. Actual conversion spacing and scheduling delay
require measurement.

The worker passes copies through a two-record `k_msgq`. Capture puts use
`K_NO_WAIT`. A full queue discards the new record and increments a cumulative
loss count. Unread captures retain their FIFO order. The foreground owns
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

The first line identifies `hvac_adc_worker`, schema version 1, the native
14-bit range, ADC channel order, sweep count and queue depth. A subsequent
`setup_status` record reports the driver's setup result. A setup error
ends the worker.

Capture records include these fields.

| Field | Meaning |
| --- | --- |
| `request` | Zero-based ADC attempt ordinal, including dropped captures |
| `dropped_before` | Total capture records rejected by the queue before this attempt |
| `clock_ok` | Both local clock reads succeeded |
| `started_us`, `returned_us` | Boot-relative reader-call bracket |
| `driver_status`, `guard_ok` | Original ADC return and buffer guard check |
| `channels` | Three ordered channel statistics with faults, sample and near-rail counts |

The channel statistics are finite-block mathematical quantities. A
16-sample block at the requested interval is insufficient to establish a
50/60-Hz motor-current RMS measurement. The probe reports ADC codes.
Failed, incomplete or out-of-range sequences suppress all channel statistics.
A near-rail observation suppresses that channel's numeric statistics while
retaining its fault and count. A constant, usable block has zero AC RMS.
The synthetic tests exercise these cases with known channel patterns.

Clock failure, guard damage or request-counter exhaustion ends ADC activity.
A final `terminal_reason` record uses 1 for clock failure, 2 for guard damage
and 3 for counter exhaustion. It includes `dropped_total`. Setup and terminal
records wait for a queue slot so the foreground can observe them. A stalled
consumer can delay those records indefinitely. The driver read itself also
has no application cancellation timeout.

## Build and manual bench checks

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --output-dir build/worker-probe firmware/current_worker_probe
```

This increment compiled the sketch and ran synthetic software tests. The
following board checks remain pending. Use the protected SELV fixtures and
input review described in [sequence-probe.md](sequence-probe.md).

1. Capture the startup and setup records under manual supervision. Apply known
   safe DC levels to each channel and check channel identity and mean codes.
2. Record reader duration, task scheduling and channel aperture behavior with
   a suitable reference instrument. Check the thread stack margin.
3. Set `CONSUMER_PAUSE_MS` to 250 for a deliberate slow-consumer run. Confirm
   increasing attempt gaps and loss counts, FIFO order and recovery after
   normal consumption resumes. Keep resets in separate captures.
4. Exercise setup and reader failures through reviewed fixtures. Verify
   failure records and latched termination without automatically restarting
   the worker.

Next is binding worker results to negotiated sessions and safely discarding
captures after stop, expiry or reconfiguration. A worker result will need
qualified per-channel timing and calibration before producing amperes.
