# Raw-code offset removal and RMS statistics

`HvacCurrent/src/RawFeatures.h` provides bounded, per-window mathematical
statistics. It does not acquire samples or convert codes to volts/amperes.
Only the synthetic self-test sketch calls it in this increment. Production
current fields remain unknown; neither ADC probe nor the environmental sketch
has been connected to this accumulator.

## Calculation and numerical bounds

For N accepted unsigned ADC codes x, the estimated DC offset is their mean.
The AC component's population RMS in **ADC codes** is:

```text
mean = sum(x) / N
AC RMS = sqrt(sum((x - mean)^2) / N)
       = sqrt(N * sum(x^2) - sum(x)^2) / N
```

The second form uses exact unsigned 64-bit integer sums and products before
converting to double for the square root/division. Both maximum code and maximum
sample count are bounded to 65,535. Thus each product is at most 65,535^4, which
is below UINT64_MAX. Integer centering avoids subtracting nearly equal rounded
floating-point quantities when a tiny ripple sits on a large DC bias.

The divisor is N, not N-1. This is RMS about the observed window's mean, not the
sample-standard-deviation estimator used in some statistical applications.
For example, 400 codes of 1950 alternating with 400 codes of 2150 give mean
2050 and AC RMS 100 codes. Constant 2048 codes give AC RMS zero, but that does
not establish zero motor current: a disconnected, stuck or incorrectly sampled
input can also be constant.

## Validity contract

Create one accumulator per channel/window. `reset(config)` clears every sum,
counter and old fault. Configuration contains the raw code maximum, diagnostic
rail margin, minimum sample count and maximum sample attempts. Minimum count
must be at least two and no greater than the maximum. The maximum is at most
65,535; twice the rail margin must be below the code maximum.

`add(int32_t)` preserves signed driver errors as invalid input instead of casting
them to valid codes. In-range observations contribute to sums/ranges. Near-rail
observations are counted but conservatively invalidate the result. Too many
attempts latch an overflow fault and no further values are accumulated.

`finish(sourceCompleteAndUsable)` defaults the source gate to false. A caller
must separately establish that it has a complete usable source window. The
boolean is a required assertion by the caller, not evidence manufactured by
this class. Unknown timing, acquisition faults, stale sessions or buffer errors
must not be silently treated as a qualified current window.

Any bad configuration, unavailable source, insufficient accepted samples,
invalid code, rail flag or overflow makes `statisticsAvailable()` false.
Mean and AC RMS stay NaN for every unavailable result. Counters and observed
raw ranges remain diagnostic information; ranges have meaning only when
sampleCount is nonzero. An in-range return from add does not override these
window/source gates. Never serialize NaN as an ordinary current reading:
future frame integration must use null plus the appropriate quality evidence.

The accumulator stores no waveform or pointers, performs bounded work per add,
and has no shared/global state. One owner must serialize its operations; it is
not a concurrent ISR/worker object. Reset is necessary for each new window or
session. There is no cached prior-window result to return on a later failure.

## What these statistics do not establish

The estimated window mean is not a separately measured analog bias voltage.
Mean subtraction cannot correct CT/ADC gain error or a drifting/clipped input.
No supplied code-to-voltage or CT calibration coefficient is implemented here.

Uneven sampling, gaps and partial mains cycles can make the observed code RMS
different from the intended electrical waveform RMS. A sample-count minimum is
not a validated window duration or a guarantee of complete 50/60-Hz cycles.
The sequence probe supplies request-level brackets, not sample timestamps, so
these formulas cannot qualify its timing by themselves. Clipping margins also
need bench qualification. No diagnostic accuracy, true watts, motor rotation,
electrical safety or acquisition throughput follows from a passing math test.

## Verification and next integration gates

Update 2026-10-19: features now retain their ADC code maximum for the separate
[window/calibration gate](current-window.md). That gate prevents scale mismatches
and applies only explicitly approved calibration records; its current fixtures
are synthetic, and production conversion remains disabled.

The shared C++ tests run natively with undefined-behavior checks and in CI with
address/undefined-behavior sanitizers. They cover constant and alternating
inputs, offset invariance, a rounded full-cycle sine, conservative rail flags,
negative/out-of-range codes, source/length/overflow gates, independent channels,
reset and the maximum 65,535-sample arithmetic boundary. One hundred varied
windows are compared with an independent centered two-pass reference.

The same tests compile in `current_block_selftest` for the pinned UNO Q core,
but have not executed on an MCU. No sketch has been uploaded or released.
Double-precision execution time and MCU numerical behavior remain bench work.

Reproduce the native checks with the existing command in
[block-contract.md](block-contract.md), or build the expanded self-test:

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --output-dir build/block-selftest firmware/current_block_selftest
```

Next integration work must establish timing provenance, session/window age and
source validity, then introduce explicitly versioned calibration from reference
measurements. Keep high-rate acquisition separate from slow I2C/bridge work,
and preserve the passive inputs and HVAC/electronics isolation boundaries.
