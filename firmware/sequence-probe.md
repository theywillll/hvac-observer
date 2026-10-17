# Blocking multi-channel ADC bench probe

`current_sequence_probe` is a separate diagnostic sketch for the pinned UNO Q
core 1.0.0. Its direct Zephyr calls are compilation-qualified, not physically
validated. It does not replace the environmental sketch or raw single-channel
probe. It has no I2C loop, RouterBridge callback, current calibration, RMS output,
worker thread, DMA engine or HVAC control output.

## What the request does

The installed devicetree maps A0/PA4, A1/PA5 and A2/PA6 to ADC1 channels 9, 10
and 11. Compile-time checks reject changed channel IDs; startup checks require
the same device, native 14-bit resolution, matching oversampling and single-ended
inputs. The core's pin-control helper initializes ADC1 and restores each analog
pin separately. Every pin-control/readiness/channel-configuration error stops
setup; no readings follow a failed setup.

One blocking `adc_read()` requests 16 sweeps of all three channels in a 96-byte
buffer. `extra_samplings=15` means 16 total sweeps, not 15. The API specifies
ascending ADC-ID order inside each sweep, so offsets 0,1,2 are A0,A1,A2, offsets
3,4,5 are the next sweep, and so on. This ordering still needs physical channel
identity tests. These are multiplexed conversions, not simultaneous samples.

The request uses `interval_us=1000`, a deliberately conservative bench setting.
It does **not** establish a 1-kHz actual rate, precise spacing, or the research
target of 8 ksample/s/channel. Interval accuracy is driver-dependent. Only
`micros()` before/after the whole blocking request is recorded. There are no
individual sample timestamps, no per-channel aperture/skew measurements and no
time continuity between requests. A nominal 100-ms loop starts requests without
catch-up; conversion and serial delays can extend that period.

The direct API returns native 14-bit codes (0..16383). The earlier slow Arduino
probe requests converted 12-bit return values (0..4095); their formats and code
scales must not be interchanged. Neither code width proves effective accuracy.

## Failure handling and isolation

Before each request, every code cell is set to an out-of-range sentinel and
adjacent guards are initialized. Only a zero driver return, intact guards and
48 in-range codes permit publication. All other cases output `raw:null` while
preserving the original driver status and guard result. In particular, Zephyr's
`-EBUSY` can mean delayed sampling despite a filled buffer; that request remains
invalid and its codes are discarded. Setup failures and guard damage latch the
probe off until manual reset. Other failed requests remain observable on later
slow retries. Guard words detect some problems after return; they cannot prevent
arbitrary memory corruption or certify the driver.

This standalone sketch owns ADC1 exclusively. Do not add unrelated analogRead,
I2C polling or concurrent callbacks. Its blocking driver call has no application
timeout or recovery watchdog and could stall on unsupported hardware/configuration.
Do not leave it unattended or integrate it into production on compile evidence.
Keep HVAC common separate from electronics ground, use protected SELV fixtures,
and never attach mains or a current-output CT directly to these pins.

The raw block pool from the previous increment expects per-channel timestamps.
This probe deliberately does not feed it invented sample times. A production
backend needs measured or hardware-qualified aperture timing, synchronization,
missing-window handling and reset/session identity before integration.

## Build and analyze

Use the pinned toolchain/libraries from [current-acquisition.md](current-acquisition.md):

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --libraries firmware/libraries --output-dir build/sequence-probe firmware/current_sequence_probe
python -m scripts.check_adc_sequence captured-sequence.jsonl
```

No board upload has been performed. A future manual capture must include exactly
one startup JSON object and consecutively numbered request objects beginning at
zero. The startup records version, native resolution, ADC IDs, sweep count,
requested interval and setup status. Keep resets in separate files.

The analyzer checks the exact format, integer/code limits, request continuity,
non-overlapping call brackets and microsecond-timer rollover. It rejects partial
rows, contradictory error/data records and mixed reset sessions. Accepted arrays
are demultiplexed by ADC ID, with raw ranges and near-rail counts per channel.
The 32-code rail margin is a diagnostic observation, not a calibrated clipping
threshold. Failed requests contribute no samples. A syntactically accepted
capture may contain only failures; inspect accepted/rejected counts and null
code ranges. The analyzer never estimates sample interval, volts, amperes or RMS.

## Bench evidence still required

No procedure below has been performed:

1. Review board/core/loader identity and the protected input/bias assembly.
2. Verify rails and midpoint with a reference instrument before supervised upload.
3. Apply different safe DC fixture levels to A0/A1/A2. Check raw channel order,
   code ranges, guard results and error statuses; record reference values.
4. Measure conversion/sweep timing with suitable instrumentation or a separately
   qualified timing backend. Request duration divided by 16 is not a sample-rate
   measurement. Do not compute motor RMS from these disconnected blocks.
5. Review driver interval behavior, blocking latency and failure recovery before
   selecting a dedicated worker and any target throughput. Use the bench evidence
   to decide whether the stock loader is adequate or another backend is needed.

Primary source inspection (2026-10-17): the core's pinned
[ADC implementation](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1afdad455f55e4a25030953617152c0227c/cores/arduino/wiring_analog.cpp),
[pin-control implementation](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1afdad455f55e4a25030953617152c0227c/cores/arduino/zephyrPinctrl.cpp),
[UNO Q overlay](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1afdad455f55e4a25030953617152c0227c/variants/arduino_uno_q_stm32u585xx/arduino_uno_q_stm32u585xx.overlay),
and the installed release's `zephyr/drivers/adc.h`. Its `adc_sequence` and
`adc_sequence_options` specify ordering/extra sampling/interval behavior.
The installed `autoconf.h` has no `CONFIG_ADC_ASYNC`; this probe neither changes
the loader nor assumes enabled ADC/DMA means continuous DMA acquisition works.
