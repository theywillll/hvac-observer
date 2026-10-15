# Raw acquisition block contract

This is a tested storage contract, separate from the ADC backend. It neither
starts conversions nor emits RMS amperes. The environmental sketch and slow
raw ADC probe do not use it yet. The input remains passive and current fields
remain unknown until a real acquisition path is qualified.

## Ownership and bounds

`libraries/HvacCurrent/src/RawBlock.h` is a C++11-compatible header with fixed
storage for three channels and a configurable number of samples and slots.
The default two slots move through `Free -> Filling -> Ready -> Reading -> Free`.
`begin()` finds a free slot. If every slot is ready or leased, it returns false
and increments a saturating overrun counter without changing unread data.
Calling begin while already filling returns false without counting an overrun.

Feed attempts in repeating channel order 0, 1, 2, then call `finish()`.
Incomplete and aborted blocks are published with faults for diagnostic reporting.
Acquisition errors retain the first negative error and set a fault. Their code
cells remain zero-initialized placeholders, never valid zero-current samples.
`usable()` requires full counts and no faults. Out-of-order/extra attempts set a
sticky fault and are rejected without indexing beyond storage.

`acquire()` leases the oldest ready block. The read-only view must be checked
before use and is invalid after release or reset. Ticket checks reject stale,
duplicate and mismatched leases. Tickets are not reused during the object's
lifetime, even across session resets. Ticket exhaustion fails closed. Objects
cannot be copied. A firmware restart still requires a fresh externally managed
capture-session identity; this class does not generate globally unique IDs.

All operations, including view access, must be serialized by one owner or an
external lock. This is **not** an interrupt-safe or lock-free producer/consumer
queue. Reset revokes every lease, including an active consumer's pointer, and
must never race with a reader. A worker/ISR backend must define synchronization
and memory visibility before integrating this contract.

## Timing and quality

Each channel retains its own count, first/last timestamp, minimum/maximum
interval and maximum conversion-call duration. Unsigned differences allow a
32-bit microsecond timer rollover. Intervals must be shorter than half the
timer range, and the backend must never silently skip a whole timer cycle.
Consecutive call brackets must not overlap or run backwards. The block also
records maximum first-to-third-channel timestamp skew within a sweep.

The backend supplies explicit minimum/maximum channel interval, maximum read
duration, code range and near-rail margin. Invalid configurations disable
acquisition. Test values are synthetic allowances, not measured UNO Q limits.
The timestamp's meaning belongs to the backend: `micros()` before analogRead
is a call-start observation, not proof of the ADC's sample aperture. First/last
times and bounds summarize timing; they do not retain every timestamp.

Near-rail flags use the selected code margin and conservatively invalidate a
block. They are not calibrated clipping thresholds or proof of input protection.
No offset subtraction, voltage/current calibration, RMS feature, watchdog,
high-rate scheduler, anti-aliasing guarantee or production frame integration is
included in this increment.

The 800-sample/channel example holds 4,800 bytes of codes per slot, plus timing,
ownership and fault metadata. `sizeof` is reported by the native test and target
self-test, because alignment differs by toolchain. This checks bounded memory,
not achievable throughput or available stack headroom on a running board.

## Reproduce the tests

From the repository root with a native C++ compiler:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ifirmware/libraries/HvacCurrent/src tests/current_block_test.cpp -o /tmp/current-block-test
/tmp/current-block-test
```

The shared test cases cover state transitions, independent channel intervals,
rollover, queue exhaustion, FIFO reuse, stale leases, partial reset, driver
errors, near-rail data, invalid configuration and invalid append order. They
also run 1,000 repeated full-queue/release cycles. These are synthetic inputs.
CI runs them under address and undefined-behavior sanitizers.

Compile the same test logic for the pinned target without uploading:

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --output-dir build/block-selftest firmware/current_block_selftest
```

The self-test sketch runs synthetic cases only and reserves the proposed
800-sample/channel pool. Compilation does not mean those tests have executed on
an MCU. Any later board upload and serial-output check is manually supervised.

## Backend evidence and next dependency

The installed core 1.0.0 headers expose blocking `adc_read`, whose inline
implementation calls the device's read API, and `adc_sequence_options` including
extra samplings and driver-dependent intervals. Its generated configuration has
ADC STM32 enabled and a 10-kHz kernel tick, without `CONFIG_ADC_ASYNC`.
The export table marks ADC implementation functions NULL, but this does not
prove the inline device dispatch is unavailable. No direct Zephyr acquisition
backend has been compiled or selected in this increment.

Next: qualify a blocking multi-channel sequence in the exact loader/core,
check driver ordering/configuration, and define worker synchronization and timing
failure handling. Hardware timing measurements and ADC/reference calibration
remain required before claiming deterministic acquisition or calibrated current.
Keep conversion work outside slow I2C polling and RouterBridge callbacks.

Sources: [pinned Arduino ADC implementation](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1afdad455f55e4a25030953617152c0227c/cores/arduino/wiring_analog.cpp),
[Zephyr ADC API](https://docs.zephyrproject.org/latest/hardware/peripherals/adc.html),
and the installed release's `adc.h`, `autoconf.h` and `syms-static.ld` checked
2026-10-15. The moving Zephyr documentation is guidance; the pinned installed
headers/configuration determine the actual target's API.
