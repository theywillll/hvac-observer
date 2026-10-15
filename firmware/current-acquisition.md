# Optional current acquisition: design and first build

Status on 2026-10-13: the environmental sketch and a separate raw ADC probe compile
for UNO Q. No board has been connected, no upload has been performed, and no
waveform acquisition or RMS-current firmware is implemented yet.

Update 2026-10-15: a [bounded block contract and synthetic self-test](block-contract.md)
are implemented separately. Hardware acquisition remains unimplemented. The
contract requires externally serialized calls; backend synchronization is still
a prerequisite to integration with a sampling worker.

## Build target

Use the published `arduino:zephyr:unoq` target, not classic AVR UNO or Ventuno Q.
The first verified build uses Arduino CLI 1.5.1, Arduino Q Boards / Zephyr core
1.0.0 and its supplied `zephyr:arm-zephyr-eabi` 1.0.1 tool package. The published
core archive is `ArduinoCore-zephyr_unoq-79b3f1af.tar.bz2` in Arduino's standard
package index. The prebuilt core/loader and its development kit are part of the
target; building a custom Zephyr loader is a different task.

```sh
arduino-cli core update-index
arduino-cli core install arduino:zephyr@1.0.0
arduino-cli lib install --no-deps 'Arduino_RouterBridge@0.4.3' 'Arduino_RPClite@0.3.0' 'MsgPack@0.4.2' 'DebugLog@0.8.4' 'ArxTypeTraits@0.3.2' 'ArxContainer@0.7.0'
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --output-dir build/environmental firmware/hvac_observer
arduino-cli compile --fqbn arduino:zephyr:unoq --warnings all --output-dir build/adc-probe firmware/current_adc_probe
```

Run from the repository root. Commands are for compilation only. Use a dedicated
CLI config/data directory when installing locally if another Arduino setup must
remain untouched. CI installs the same versions and retains build artifacts and
hashes. Do not upload or update a board loader as part of an unattended build.

## What the target actually provides

The official UNO Q documentation identifies the STM32U585 MCU and 3.3-V MCU
interfaces, separate from other MPU connectors. In the inspected core 1.0.0:

| Signal | MCU pin | ADC peripheral/channel | Project circuit |
|---|---|---|---|
| A0 | PA4 | ADC1 / 9 | Compressor, J201 |
| A1 | PA5 | ADC1 / 10 | Blower, J202 |
| A2 | PA6 | ADC1 / 11 | Fan, J203 |

These are multiplexed channels on **one ADC**, not three simultaneous converters.
The supplied devicetree sets these conversions to 14 bits. `analogReadResolution`
changes the returned code width in `wiring_analog.cpp`; requesting more bits does
not create additional physical resolution. The probe requests 12-bit output,
so this core shifts the 14-bit conversion down by two bits. Neither figure
establishes effective accuracy.

`analogRead()` configures a single channel, submits a blocking `adc_read` with a
single-sample buffer and returns either a code or a negative error. It does not
provide a continuously timed, three-channel capture stream. Do not interpret a
negative error as an unsigned sample or zero current.

The supplied generated configuration enables ADC and DMA, has a 10,000-Hz kernel
tick, and does not enable `CONFIG_ADC_ASYNC`. DMA being enabled elsewhere is not
proof of ADC DMA support. Zephyr's `adc_sequence_options.interval_us` documentation
explicitly makes timing driver-dependent; a kernel-timer implementation at this
tick rate cannot directly express an exact 125-us / 8-kHz cadence. Device APIs,
exported symbols, interrupt/thread context and loader configuration must be
checked before selecting an implementation. A build passing does not prove the
ADC can sustain a particular rate.

## Proposed waveform architecture (not implemented)

1. Give the acquisition backend exclusive ownership of ADC1 for A0-A2. Do not
   mix it with unrelated `analogRead()` calls while a block is active.
2. Keep acquisition outside the environmental loop's I2C delays and outside
   RouterBridge callbacks. Use a dedicated worker with a qualified hardware
   trigger/buffered driver if supported. A software-timed fallback must expose
   lateness and reject unsuitable windows, rather than claiming determinism.
3. Preserve each channel's sampling interval and conversion skew. Three channel
   calls per sweep do not mean each channel has the aggregate conversion rate.
   The hardware reference's 8-ksample/s-per-channel target requires at least
   24,000 conversions/s plus overhead and a measured timing/error budget.
4. Use bounded buffers with explicit ownership and overrun reporting. An initial
   100-ms research block at 8 ksample/s/channel would hold 2,400 uint16 codes:
   4,800 bytes per block or 9,600 bytes for two blocks, excluding metadata.
   Memory arithmetic is not a throughput result.
5. Reject acquisition errors, incomplete blocks, clipping and unacceptable
   timing before offset removal and RMS calculation. Subtract the measured
   per-channel mean; apply the exact CT and ADC/reference calibration only after
   those coefficients have been established. Never infer true watts from amps.
6. Publish window age, sample counts, per-channel timing, validity and calibrated
   current only after qualification. Until then current remains unknown in the
   production frame. Reset session identity and invalidate buffers/features on
   startup or acquisition failure; never splice old and new sequence histories.

If the stock loader cannot provide the required driver/timing, evaluate a reviewed
custom loader or a separate acquisition node. Do not silently replace the target
or invent supported DMA settings. The existing single-pole RC filters also need
an anti-aliasing review against the selected rate and desired bandwidth.

## First bench tool: raw-only ADC probe

`current_adc_probe/current_adc_probe.ino` is a **separate diagnostic sketch**.
It has no I2C polling, RMS output, current calibration or frame integration.
Every nominal 100 ms it reads A0, A1 and A2 sequentially, storing each call's
`micros()` start and elapsed time before printing CSV. Serial output can delay
the next sweep, and there is no catch-up burst. It is deliberately slow; it is
not a motor-current sampler or evidence that 8 ksample/s is possible.

The timestamps bracket API calls, not the ADC's sampling aperture. Unsigned
subtraction handles timer rollover for intervals shorter than half the 32-bit
range. Sequence/session checks in the capture analyzer reject missing sweeps,
incomplete channel sets and resets instead of quietly combining them.

The `near_rail` flag uses an eight-code margin around the requested 12-bit range.
It is an observation flag, not a calibrated clipping threshold or proof of ADC
protection. Raw errors and out-of-range codes remain visible. A valid CSV file
means only that the capture format/order is consistent; it does not mean the
measurements are good or the circuit is safe.

```sh
python -m scripts.check_adc_probe captured-probe.csv
```

Analyze one reset session per file. The analyzer reports per-channel code ranges,
error/near-rail counts, read-call duration and inter-sweep interval ranges. It
does not convert to volts or amperes. Its tests use synthetic CSV, not ADC data.

## Bench procedure to perform with appropriate equipment

No steps below have been performed. A physical UNO Q, reviewed input assembly,
protected low-voltage fixture, scope/logic analyzer and a reference meter or
signal source are required.

1. Review the exact board/pinout, source ratings, bias circuit and clamp limits.
   Keep HVAC_C separate from electronics ground. Disconnect field HVAC wiring;
   use only reviewed SELV test signals within the board's allowed input range.
   Do not attach mains, a current-output CT or arbitrary negative AC to A0-A2.
2. With power isolated as appropriate, inspect wiring and shorts. Power the
   fixture under current limit, verify the rail and stable midpoint with a
   reference instrument, and account for reference-ground connections.
3. Under manual supervision load the probe, open the board's documented serial
   monitor and record its startup marker/header plus complete sweeps. Test known
   safe DC levels around midpoint independently on each channel. Compare raw
   order and channel identity. No particular code-to-voltage accuracy is assumed.
4. Capture the calls' timing and independent per-channel cadence. Examine delays
   during serial activity, reset and reference-input changes. Do not use these
   slow readings to compute a 50/60-Hz waveform RMS value.
5. Capture each reset separately; run the analyzer. Review reported errors and
   near-rail observations rather than treating parser success as a pass criterion.
   Record instrument identity, fixture settings, exact versions and raw captures.
6. Agree a measurement/timing acceptance budget before developing and accepting
   high-rate buffered acquisition. Field CT installation remains a qualified
   technician task after circuit and acquisition validation.

## Primary sources and reproducibility

Checked 2026-10-13; the inspected release is 1.0.0, not a moving `main` build.

- [UNO Q official manual](https://docs.arduino.cc/resources/datasheets/ABX00162-datasheet.pdf)
- [Arduino core installation and loader model](https://github.com/arduino/ArduinoCore-zephyr)
- [Published package index](https://downloads.arduino.cc/packages/package_index.json)
- [Release-source ADC implementation](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1af/cores/arduino/wiring_analog.cpp)
- [Release-source UNO Q overlay](https://github.com/arduino/ArduinoCore-zephyr/blob/79b3f1af/variants/arduino_uno_q_stm32u585xx/arduino_uno_q_stm32u585xx.overlay)
- [Zephyr ADC API](https://docs.zephyrproject.org/latest/hardware/peripherals/adc.html),
  compared with the actual release's supplied `zephyr/drivers/adc.h`

The supplied release's generated `autoconf.h`, `devicetree_generated.h` and
`syms-static.ld` are the concrete loader/configuration evidence. Record their
hashes alongside binaries when reviewing a backend or changing the core.
