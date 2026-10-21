# Current snapshot lifetime contract

`HvacCurrent/src/CurrentSnapshot.h` adds fixed storage for the latest result of
each of the three current channels. It is a boundary between the existing
[window gate](current-window.md) and a future nullable frame adapter. Only shared
synthetic self-tests use it. It is not connected to the ADC probes, environmental
sketch, RouterBridge or backend. No production current values are enabled.

## Why freshness must be checked again

A window can pass its age check when processed and become stale before a frame
is requested. Retaining a previous successful amperes value after a failed
window would also conceal a measurement problem. The store therefore applies
these rules:

| Event | Result |
| --- | --- |
| Constructed or unsuccessful reset | All channels unavailable: `WindowConfig` |
| Successful reset | All channels unavailable: `WindowPending` |
| Correct session/channel publication | Replace that channel, including failures |
| Duplicate, gap, bad feature, timing or calibration | Previous value revoked; keep gate failure reasons |
| Wrong session/channel publication | Reject without modifying the active session |
| Snapshot request | Recompute each observed channel's age at the request time |
| Age above the policy limit | Latch `WindowAge`; numeric value becomes unavailable |
| Stop | Revoke every channel with `WindowStopped`; reject publications |
| Regressing extended clock | Revoke every channel with `WindowClock`; reject publications |
| Strictly newer valid session | Clear old results and begin pending again |

An expired channel can recover only with a fresh, accepted publication. The gate
still consumes correctly addressed failed window ordinals; a caller cannot
resubmit a bad window with revised metadata. Stop/clock failure requires a new
session. All reset attempts clear cached results, including unsuccessful ones.
Each channel has independent window identity, result, sample end and age.

`publish` returning true means a publication was processed, **not** that its
measurement passed. Inspect `CurrentWindow::available()` and its fault mask.
Default, failed, expired and revoked results carry NaN internally. A future
JSON adapter must emit `null`, never JSON NaN or zero, and retain quality reasons.

## Extended clock and epoch binding

Store calls take a monotonic **64-bit microsecond clock**. Publication also takes
the last sample's time extended into that same clock domain. Its low 32 bits must
match `WindowStamp::lastUs`; the existing gate independently checks window span,
aperture provenance and interval bounds using the low counter. The store checks
the full end-to-now difference. This prevents a whole skipped 32-bit cycle from
making an old end timestamp appear recent. A future end time is invalid and
cannot become valid merely by waiting until that time passes.

The caller must establish the extended sample epoch correctly. Matching low bits
cannot prove that an invented high word is truthful. The store does not extend
`micros()`, persist sessions or generate timing evidence. Trusted acquisition
code must bind features, aperture stamps and extended sample end to the same
window. Existing call-bracket probes do not provide that evidence.

Every accepted active-session operation observes one nondecreasing clock.
Backward time fails closed. A new session can establish a fresh clock origin;
globally unique/persistent boot identity remains an external requirement.
Wrong-session/channel callbacks cannot poison the active clock. Ages at or above
`UINT32_MAX` use that sentinel, remain unavailable, and never wrap into a small
age. The subtraction uses the full 64-bit range without an overflowing addition.

Example using **synthetic** limits: a window ends at 16,000 us, is published at
17,000 us and has a maximum age of 5,000 us. It is usable when requested at
21,000 us and unavailable at 21,001 us. A request more than one 32-bit cycle
later remains unavailable. These values are test fixtures, not a selected
sample rate, accuracy budget or production policy.

## Ownership and frame boundary

The store cannot be copied. It owns three results and one gate; no dynamic
allocation or growing queues are used. **All reset, publish, stop and snapshot
calls require one owner or external serialization.** Snapshot requests can
mutate expiry/clock state. This is not a lock-free queue, ISR API or implemented
background worker. Use separate synchronization from the raw-block pool when
integrating concurrent sampling; do not hold an acquisition lock during I2C or
RouterBridge operations.

A returned `CurrentSnapshot` is a detached value valid at `requestedUs`. It must
not be cached and later presented as fresh. The future frame callback needs to
request a fresh snapshot, preserve session/window identity and quality metadata,
and enforce a justified transport/arrival-age budget. The
[current wire contract](current-wire.md) now implements bounded serialization
and backend validation of available/null states and calibration ownership.
It explicitly carries observed-window state instead of treating sequence zero
as unobserved. Transport, consumer expiry and the concurrent worker remain
**pending**; the writer is exercised only by synthetic tests.

## Verification and next step

Shared tests run natively and compile in the UNO Q self-test. They cover default
and pending states, independent channels, inclusive expiry boundaries, a skipped
full timer cycle, ordinary low-counter rollover, future/mismatched end times,
failed-window replacement, duplicates, recovery, wrong-session callbacks,
stop/reset, invalid policies and backward-clock publication/request paths.
All gains, samples, timestamps and qualification IDs are synthetic.

See [snapshot-build-record.json](snapshot-build-record.json) for this increment's
local build evidence. CI runs the shared tests with address/undefined-behavior
checks and compiles all four sketches. Compilation is not board execution.

Next: define and test the nullable wire contract across C++ and Python, including
quality validation, serialization bounds, freshness at frame request and
calibration ownership. Then integrate a synchronized acquisition worker behind
unqualified defaults. A physical UNO Q, protected input fixtures and reference
instruments remain necessary for sample-aperture qualification, CT calibration,
timing/accuracy measurements and a reviewed bench procedure before enabling
current estimates. No hardware upload, HVAC output or isolation change occurs.
