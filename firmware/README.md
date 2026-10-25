# Firmware and acquisition adapters

[Live current expiry](current-live.md) now carries receipt deadlines through
ingestion and masks retained readings in status/dashboard consumers. Historical
measurements remain unchanged. Tests use synthetic clocks/DOM; MCU production,
hardware timing and browser real-time behavior remain unqualified.

The [UNO receiver](current-receiver.md) now bounds/parses snapshot text and checks
frame/window replay, session changes and conservative current receipt deadlines.
Synthetic tests exercise the real adapter. Bridge latency remains unqualified;
consumer expiry is handled separately above. No MCU current producer is enabled.

The [current wire contract](current-wire.md) now tests bounded JSON serialization
against strict Python quality validation and prevents duplicate server current
calibration. Numeric hardware current requires quality metadata. Legacy all-null
hardware frames still work. The environmental sketch is unchanged; no live
sampling, Bridge transport integration or consumer-age guarantee is enabled.

The [current snapshot store](current-snapshot.md) now tests per-channel result
replacement, expiry at request time, extended-clock rollover and stop/reset
invalidation. It uses fixed storage and requires external serialization. Only
synthetic self-tests use it; the nullable wire adapter and acquisition worker
remain pending, and production current fields remain unknown.

The [current-window gate](current-window.md) now tests session/sequence,
freshness, timing provenance and pinned calibration revisions before conditional
code-to-amp conversion. Only synthetic self-tests invoke it. No real timing or
calibration record is approved, and production current fields remain unknown.

[Raw-code feature calculations](raw-features.md) now implement per-window mean
removal and AC RMS with strict invalid-window gates. Shared native/UNO Q
self-tests cover the arithmetic, but only native execution has occurred. These
are ADC-code statistics, not calibrated current, and are not integrated with
the probes or environmental sketch.

The [multi-channel sequence probe](sequence-probe.md) compiles direct blocking
Zephyr ADC calls for the pinned core. It requests 16 sweeps of A0-A2 in one
buffer and reports request-level timing/errors as JSON lines. Its analyzer
keeps actual sample timing unknown. It has not run on a board and is not an
RMS-current backend or part of the environmental sketch.

The [raw block contract](block-contract.md) now provides fixed-capacity storage,
serialized ownership, per-channel timing bounds and explicit invalid-block flags.
Its shared synthetic tests run natively and compile in a separate UNO Q self-test
sketch. It is not integrated with an ADC backend or the production frame, and
does not calculate calibrated RMS current.

`hvac_observer/hvac_observer.ino` is an UNO Q sketch using `Wire` and `Arduino_RouterBridge`, with no third-party sensor libraries. It implements dual SHT31 acquisition, SDP810 triggered reads, CRC checking, nullable channels, isolated-demand pulse latching and an optional independent float. All input-only controls are disabled until installation is commissioned.

The verified compilation target is `arduino:zephyr:unoq` with Arduino CLI 1.5.1 and core 1.0.0. See [the pinned build commands and acquisition design](current-acquisition.md). Board loading remains a manually supervised bench step. On the Linux side, run the Python service within an environment that exposes `arduino.app_utils.Bridge`:

```sh
python -m backend.server --source uno --config config.example.json
```

RouterBridge can return a `String` from a `provide_safe` callback. The callback returns the cached frame and performs no nested RPC or hardware I/O. [Official library documentation](https://github.com/arduino-libraries/Arduino_RouterBridge).

The sketch compiled for UNO Q on 2026-10-13; no board has been connected or tested. [Build versions and hashes](build-record.json) record compilation only. Validate I2C timeouts, stuck-bus recovery, heap usage, MCU reset behavior, watchdog and 50/60-Hz pulse capture before leaving the device unattended. The environmental polling loop is not a waveform acquisition engine.

The first empty MCU frame may precede acquisition; start the Linux reader after the sensors have initialized. A repeated MCU sequence causes acquisition to stop visibly. Restart the service after a device reset (new session) instead of silently splicing sequence histories.

Pi alternative: install `smbus2==0.5.0`, enable I2C through the supported OS configuration, wire the two sensors as documented, then use `--source pi`. This adapter implements two SHT31s; other channels remain null. It has software/CRC tests, not physical Pi validation.

Expanded current, vibration, 1-Wire remote probes and a real-power meter need their corresponding acquisition tasks. See the data contract, reference circuits and calibration notes before adding those drivers. Use `--source jsonl --input readings.jsonl` for a tested external feature acquisition process producing fresh frames.

`current_adc_probe/` is a separate, compiled raw ADC bench tool with per-channel read-call timing and visible errors. It deliberately runs slowly and provides no RMS/current acquisition or production-frame integration. Its [capture analyzer](../scripts/check_adc_probe.py), [bench instructions](current-acquisition.md) and [development record](progress.md) distinguish implemented tools from the planned waveform driver.
