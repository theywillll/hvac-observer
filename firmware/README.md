# Firmware and acquisition adapters

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
