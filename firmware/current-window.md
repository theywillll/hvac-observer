# Current-window qualification and calibration contract

`HvacCurrent/src/CurrentWindow.h` is a software gate between raw-code RMS
statistics and a possible current estimate. Only shared synthetic tests call it
in this increment. There is no live calibration file, qualified timing backend,
board capture or production-frame integration. Current remains unknown in the
environmental sketch. Existing ADC probes still provide unqualified timing.

## Session and window identity

`reset(session, policy)` establishes a nonzero 64-bit session. Sessions must
increase strictly within a gate object's lifetime. Every reset attempt revokes
the active configuration and clears sequence state; invalid/reused sessions
fail closed. An external persistent boot/session authority is still required
across object destruction, MCU resets and Linux reconnects. This class does not
implement that authority or generate globally unique identities.

Each of three channels has an independent 64-bit window sequence, starting at
zero. Duplicates/backward windows are rejected. A gap invalidates its first new
window and consumes that ordinal; the next contiguous window can recover.
Correctly addressed ordinals are also consumed on data/timing/calibration failure,
so retrying a failed window with changed metadata cannot resurrect an old value.
Wrong sessions/channels do not advance another channel's state. Sequence wrap
requires a fresh session; it never silently restarts at zero.

The gate returns no cached previous current on failure. Numeric output remains
NaN unless every check passes. A future frame adapter must serialize unavailable
measurements as null with reasons; it must not emit non-standard JSON NaN.

Gate objects cannot be copied, avoiding accidental forks of sequence/session
state. Unknown age is represented internally by UINT32_MAX and must also remain
unavailable in a future external frame.

## Feature, timing and freshness gates

The policy defines expected sample count, ADC code maximum, per-channel interval
bounds and maximum age. Raw features now carry their code maximum explicitly,
preventing a 12-bit/14-bit scale mismatch from silently reaching conversion.
The gate requires full matching counts, no feature faults/rail observations,
finite internally consistent mean/RMS/ranges and the expected code maximum.

Timing must identify **qualified sample-aperture timestamps**, with a nonzero
external timing-evidence ID. Unknown timestamps or analogRead/request call
brackets are rejected even if their numeric intervals look plausible. Every
expected interval must have been observed. Observed min/max gaps must meet the
policy, and first-to-last span must agree with those bounds. An upstream backend
must bind timing and feature data to the same channel/window; this gate cannot
infer that binding from sample values.

Policy limits need independent engineering/bench justification. A numerically
consistent window does not establish enough 50/60-Hz cycles, anti-aliasing,
sensor connectivity or the desired signal bandwidth. This contract does not
automatically choose those limits or establish an accuracy budget.

Age is measured from the last sample to the caller's current microsecond clock.
All times must come from the same session/clock domain. Unsigned subtraction
allows one 32-bit rollover within a strictly bounded observation horizon. The
policy's maximum block span plus maximum age must stay below half the counter
range. Future end times, excessive age and inconsistent spans remain invalid.
Whole skipped counter cycles, wrong clock units or fabricated timestamps cannot
be detected from modulo timestamps alone; the backend must prevent them.

The `QualifiedAperture` enum and evidence ID are declarations by trusted calling
code, not proof produced by this module. Do not label callback completion times
or requested timer intervals as sample apertures without a justified timing
model and reference measurements. None of the current probes qualifies.

## Calibration schema 1

The contract permits a measured, through-origin gain in amperes per **raw ADC
code** of AC RMS. It deliberately supplies no default 3.3-V reference, nominal
CT multiplier or undocumented coefficient. A record must contain:

- Schema 1, a nonzero immutable record ID and revision, and an evidence ID.
- Its exact logical channel and ADC code maximum.
- An explicit bench-qualified flag tied to independently reviewed measurements.
- A finite positive amps-per-code gain and the RMS-code range where it applies.

The active policy independently pins each channel's approved calibration ID and
revision. Older or unrelated records cannot pass just by setting benchQualified.
An all-zero ID/revision pair disables calibration for that channel; a partially
specified pair makes the policy invalid. Changing an approved record/revision
requires a new session/policy, revoking in-flight old-session windows.

Accepted raw RMS must be inside the calibrated range; the gate never extrapolates.
Unsupported schemas, ADC/channel mismatches, absent approval, nonfinite/negative
coefficients and numerical overflow/underflow are rejected. If all checks pass:

```text
rmsAmps = acRmsCode * measuredAmpsPerCode
```

Synthetic tests use a coefficient of 0.01 only to check arithmetic (100 codes
becomes 1 test amp). That number is not a calibration for this project. No real
record or approved production policy exists. Returned evidence/calibration IDs
enable lookup in a future immutable registry; storage, signatures, revocation
distribution and provenance verification are not implemented here.

A bench record must bind the actual board/ADC configuration and CT/front-end
identity, reference instrument, method/date, observed operating range, residual
error/uncertainty and source captures. A gain-only model is inappropriate if
measurements require a nonlinear or additive RMS correction; another reviewed
schema would be needed. A qualified flag cannot replace those measurements.

## Tests, remaining integration and bench work

Shared native tests cover default rejection, per-channel order/gaps, stale and
future data, 32-bit clock rollover, session reset/replay, sequence exhaustion,
unqualified call brackets, inconsistent timing, invalid features, wrong/old
calibrations, disabled channels, range boundaries and numeric underflow. They
also check recovery using fresh windows after failure/revision changes. All
records, stamps and evidence IDs in the tests are explicitly synthetic fixtures.

The shared logic compiles in the UNO Q self-test; it has not run on a board.
No worker, high-rate driver or JSON frame adapter is connected. All access must
be serialized by one owner or external synchronization. The gate performs no
hardware I/O and changes no HVAC isolation or input-only boundaries.

Next, integrate session/window invalidation with a bounded acquisition worker
and nullable frame contract without inventing aperture timing. Before enabling
real current, measure cadence/skew/gaps, establish a fresh-session mechanism,
review aliasing/clamp limits and derive calibration from protected reference
fixtures. Constant/stuck/disconnected inputs can still look like zero RMS;
this gate does not diagnose those conditions or prove motor rotation, true
watts, equipment health, measurement accuracy or electrical safety.
