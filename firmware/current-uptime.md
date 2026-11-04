# UNO Q boot-relative uptime

The optional current self-test now compiles a reader for Zephyr's full
`k_uptime_ticks()` count. The pinned Arduino Zephyr 1.0.0 UNO Q variant
defines `CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000` in its generated autoconf header.
The adapter enforces that configuration at compile time. See
[Zephyr kernel timing](https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html#uptime).

`CurrentUptime` converts ticks to nominal microseconds at 100 us per tick,
without reconstructing high bits from `micros()`. The pinned Arduino core's
`zephyrCommon.cpp` returns a 32-bit microsecond conversion from its cycle
counter; that is a separate clock interface. Never mix values from these
interfaces or interpret either read as an ADC aperture timestamp.

One serialized owner holds the reader for an MCU boot. Equal ticks are valid;
negative, decreasing or unrepresentable ticks latch a fault. Failed reads
return false and clear the output rather than reusing a prior value. The
caller must check success before using the output; zero itself is a valid
boot-relative reading. There is no reset operation that can clear a fault on
session change. A new object cannot prove a new boot; binding the owner to an
actual boot and the reserved host session is still pending.

The 100-us step is nominal kernel resolution, not measured accuracy or
latency. Neither the reader nor the synthetic tests qualify quantization,
clock rate, low-power behavior, ADC timing, sample cadence or transport age.
The clock is not wired into production acquisition or the snapshot store.
A future worker must invalidate its current state on clock failure and must
use one qualified time domain for sample ends and freshness checks; it may
not substitute a coarse uptime read for sample-aperture evidence.

Native tests cover equal ticks, conversion, the low-32-bit boundary, gaps
spanning multiple wraps, regression, negative ticks and overflow. The UNO Q
self-test includes a two-read smoke check that would run if flashed; this
increment compiles it but does not run it on a board. No GPIO, ADC, I2C or
Bridge changes are part of this increment.

Next: bind the host reservation to an explicit MCU boot/session handshake
with an acknowledgement, then connect a serialized worker behind unqualified
defaults. Bench timing and calibrated current still require a UNO Q and
protected reference fixtures.
