# Current acquisition bench handoff

Reviewed 2026-11-25 against
[`94c1317`](https://github.com/theywillll/hvac-observer/commit/94c1317c51a4e828fb12a29549a8e578f922ab19).
The software foundation has reached a bench dependency. Recurring development
ends here while measured timing and calibration evidence are pending.
Calibrated motor-current acquisition remains unfinished.

## Readiness audit

| Area | Verified software | Physical work pending |
| --- | --- | --- |
| Statistics | Channel demultiplexing, offset removal, raw-code RMS, rail/error handling | ADC response, noise and useful bandwidth |
| Sessions | Reservations, acknowledgements, expiry, replay and delayed-completion tests | Real resets, entropy and connection loss |
| Worker | Pinned compile, bounded queues, exclusive ADC owner | Scheduling, stack margin, backpressure and blocked reads |
| Frames and host | Nullable metadata, size rejection, receipt and consumer expiry | Bridge envelope and latency under load |
| Environmental integration | Default and worker-enabled builds | Concurrent sensor reads and failure recovery |

The baseline's [prototype workflow](https://github.com/theywillll/hvac-observer/actions/runs/45487626893)
passed Python/dashboard checks. Its
[firmware workflow](https://github.com/theywillll/hvac-observer/actions/runs/45487626826)
passed native sanitizer/fixture checks and all six pinned builds. Local
verification recorded 97 Python tests and 16 wire fixtures.
The environmental worker defaults to disabled. Its opt-in path reports null
current with timing/calibration faults. See the [worker guide](worker-probe.md).

## Dependencies for further acquisition work

The current reader requests 16 sweeps of multiplexed ADC channels 9–11 at
a nominal 1,000-us interval. The worker sleeps 100 ms after each request.
Clock brackets describe complete reader calls. Per-channel aperture timing
and conversion skew still require measurement.

The research target of 8 ksample/s per channel needs a qualified backend.
The pinned core uses 100-us kernel ticks and leaves ADC asynchronous support
disabled. Actual driver interval generation and throughput are unverified.
Measurements will determine the backend, window length and any required
core/driver change. See the [acquisition design](current-acquisition.md).

The calibration contract needs measured total-chain amperes per ADC code,
channel identity, usable range and versioned evidence for the actual
voltage-output CT, conditioning circuit and ADC. The
[window gate](current-window.md) validates supplied records. Its test
coefficients are synthetic.

## Evidence needed from the bench

Use a physical UNO Q with its board revision recorded, the reviewed input
assembly, protected low-voltage fixtures, an appropriate reference meter or
signal source and an oscilloscope/logic analyzer suited to the timing check.
Follow the existing [input review](../hardware/README.md),
[bench procedure](current-acquisition.md) and [sequence probe](sequence-probe.md).

1. Record board, wiring, firmware commit, toolchain/library versions, build
   flags and binary hashes. Check bias, channel identity, allowed input
   range, clamp behavior and isolation boundaries.
2. Retain raw captures and independent per-channel timing observations.
   Establish interval, jitter, skew, missing samples and driver status at
   the intended rate, including sensor and Bridge load.
3. Agree the signal bandwidth and error budget with the lab. Select
   cadence and window length from measured behavior, including anti-aliasing.
4. Measure each complete CT/conditioning/ADC chain against a reference.
   Retain instrument identity/settings, reference readings, gain calculations,
   usable range, noise, near-rail behavior and uncertainty.
5. Exercise host restart, MCU reset, expiry during a read, delayed completion,
   slow consumers, sensor failures and connection loss. Measure stack margin
   and worst-case response delay.
6. Check the complete combined frame with populated window/timing/calibration
   IDs against the 896-byte application limit and real Bridge envelope.
   Oversize payloads return an error. A numeric producer may need a revised
   frame budget or schema.

No board or HVAC equipment was operated for this audit.

## Resume point

With bench captures, hardware/instrument details, a chosen timing/error
budget and reviewed calibration records, implement the qualified backend
and per-channel timestamp mapping. Configure complete waveform windows,
bind the measured calibrations to channel policies and connect qualified
features to the numeric frame. Then verify the host/dashboard pipeline
and measured accuracy before a field trial.
