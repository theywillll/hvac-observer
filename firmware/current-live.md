# Expiry of current evidence in live consumers

The UNO receiver now carries its conservative per-channel host deadlines into
the service as **process-local context**. Ingestion, `/api/status` and the
dashboard apply expiry checks to retained readings. This extends
[receipt checks](current-receiver.md); it does not enable an MCU current producer
or qualify clocks, ADC timing, router capacity, calibration or hardware accuracy.

## Trusted receipt context and ingestion

`ReceivedFrame` is a dictionary with a frozen `CurrentReceipt` attribute:
receipt time, three monotonic deadlines and a binding to the frame sequence,
timestamp, source, current values and quality metadata. JSON serialization does
not include this attribute. Queueing and ingestion do not start a new lease.
The service validates the binding and clock before using numeric hardware current.
A copied JSON dictionary or modified payload cannot inherit the original lease.
This is a trusted in-process boundary, not cryptographic authentication against
local Python code. Persistent boot identity and provenance approval remain pending.

Environment-only/all-null hardware frames still work without receipt context.
Explicit simulator/replay data remains synthetic/historical. Numeric hardware
current from a generic JSONL spool now needs a future trusted adapter; its loose
wall-clock-age check cannot mint microsecond host deadlines. Do not relabel real
hardware as simulation to bypass this rule. Pure wire validation alone does not
qualify live ingestion.

The later [host-session ledger](current-sessions.md) also requires nonnull UNO
current sessions to match a reservation supplied independently by the service.
It supplies host allocation history, not an MCU boot handshake or clock proof.

If a trusted window expires between receipt and ingestion, its current becomes
null before entering the engine, with an unavailable copied quality entry and
age-fault bit. It is never interpreted as zero or proof of motor stoppage.
Original input remains in historical `raw_values`; ingested `values` represent
evidence available at ingestion time.

## Live API copy versus historical record

Status reads create a detached copy and check each deadline again. Additional
`current_live` metadata has schema 1 and three ordered channel objects:

```json
{"schema":1,"channels":[
  {"field":"compressor_a","available":true,"remaining_us":1000,"reason":null},
  {"field":"blower_a","available":false,"remaining_us":null,"reason":"expired"},
  {"field":"condenser_fan_a","available":false,"remaining_us":null,"reason":"unavailable"}
]}
```

This is a synthetic shape example. Hardware remaining budget is the nonnegative
floor of `(deadline - now_ns)/1000`; unknown budgets are null. Expiry, acquisition
error, whole-frame staleness, missing receipt or a regressing host clock cannot
leave numeric current in the live copy. Error/clock revocation latches until
successful new ingestion. The worker reports failures under the application lock;
the next status response clears currents without waiting for the 15-second
whole-frame stale threshold. This does not cancel a hung RPC.

Source `age_us` is not rewritten with a different clock. Copied quality flags may
add consumer invalidation (`WindowAge`, `WindowClock`, `WindowTiming` or
`WindowStopped`) and set availability false. `current_live.reason` distinguishes
that consumer decision from a source/ADC fault. Original quality is in history.
`raw_values` is omitted from live status, preventing accidental use of that
historical numeric copy as a current reading.

When evidence expires, `latest.live_limited=true`, motor states/mode become
unknown, diagnostic metrics are withheld and current-dependent alerts are
suppressed. The independent condensate warning remains explicitly marked
`last_observation` with its original timestamp, not fresh proof of water level.
No rules or events are recomputed during reads. Metrics are conservatively
withheld together, including aggregates still accessible in history.

The retained result, engine state and SQLite history are never mutated by status
reads. A valid-at-ingestion current can remain in `/api/history` as a timestamped
historical observation. Charts explicitly label samples historical. Historical
runtime summaries remain prototype estimates from observations, not proof of
continuous motor operation between samples. Other sensor tiles retain their
existing whole-frame staleness handling; this increment addresses current evidence.

## Browser retention

The browser charges the entire status/history fetch duration against remaining
budgets before first display, then ages the response using `performance.now()`.
An expiry timer rerenders independently of the two-second poll. Focus/visibility
events recheck; network failure latches until a successful response, so focus
cannot revive an offline copy. A response without a latest sample clears old
tiles during service startup/reset. Missing current or withheld runtime is not
formatted as zero. The live health heading shows insufficient evidence; the
preserved float warning is labelled as a last observation.

Browser timers, background-tab scheduling and paint timing are **not hard real
time**. These are conservative display checks, not a proven microsecond visual
deadline or safety function. Synthetic DOM tests verify logic; no actual browser
timing or device experiment occurred. Very short leases may expire before useful
display, which is preferable to extending them without evidence.

## Verification and next step

Python tests cover ingestion expiry, independent/exact deadlines, immutable
history/engine state, missing/altered receipts, clock errors, error clearing,
float-warning retention and actual HTTP status/history separation. Node tests
execute the real app with a synthetic DOM, fetches, clocks and timers, covering
expiry, transit, offline/focus latching, visibility return and startup clearing.
Existing C++/Python fixtures and unchanged pinned UNO Q compilation also pass.

Run `python -m unittest discover -s tests -v` and `node tests/current_view_test.js`.
See [live-build-record.json](live-build-record.json) for local evidence and the
commit workflows for CI results. No hardware upload, physical latency, field
diagnosis or calibrated measurement is claimed.

Next: persistent acquisition-session and extended-clock ownership, followed by
the bounded router envelope and synchronized worker, without inventing aperture
timestamps. Protected board/reference measurements are still needed for cadence,
skew, clock margins, anti-aliasing, front-end limits, calibration and accuracy.
