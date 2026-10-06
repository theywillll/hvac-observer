# Architecture

## Platform decision

UNO Q is not an ATmega328P Uno. It contains Linux and MCU domains. Use its 3.3-V MCU headers; the MPU has 1.8-V interfaces on other connectors. The MCU ADC offers selectable resolutions up to 14 bits; that does not establish effective accuracy or sustained application throughput. See the [official manual](https://docs.arduino.cc/resources/datasheets/ABX00162-datasheet.pdf) and [Arduino's core documentation](https://github.com/arduino/docs-content/blob/main/content/hardware/02.uno/boards/uno-q/tutorials/01.user-manual/content.md).

| Criterion | UNO Q alone | Raspberry Pi alone | UNO Q + Pi |
|---|---|---|---|
| Analog acquisition | MCU A0–A5; conditioned 0–3.3 V only | No general-purpose header ADC; external ADC needed | Same MCU ADC; Pi adds none |
| Sensor expansion | 6 analog, D0–D13 functions; I2C/SPI peripherals; shared pins must not be double allocated | I2C/SPI/USB, external ADC and I/O expanders | Same sensor capacity plus distributed nodes |
| Sampling target | 0.5 Hz environmental sketch; high-rate waveform work needs separate DMA/timing firmware | Slow I2C polling feasible; Linux timing unsuitable for precision software-timed waveforms | MCU handles timing; network moves features |
| Isolation | Must be designed externally | Must be designed externally | Still required at HVAC inputs; network boundary preferred |
| Real-time behavior | MCU suitable for deterministic acquisition, but supplied polling driver must be bench-qualified | Linux acquisition has scheduling jitter | MCU does acquisition, Pi only orchestration |
| Python / ML | Linux Python, robust statistics and small ML models | Python ecosystem; choose Pi 4/5 for heavier models | Ample but redundant for one system |
| Database / dashboard | SQLite and local dashboard on MPU | SQLite and local dashboard | Pi can centralize multiple units |
| Networking | Wi-Fi; local Bridge MCU↔MPU | Wi-Fi/Ethernet varies by Pi | Wi-Fi/TLS between Linux hosts; no long GPIO cable |
| Board cost | $59 2GB planning basis | Zero 2 W advertised $15; accessories extra | Sum of both boards and supplies |
| Development | App Lab/core integration plus Linux | Simplest low-rate all-digital path | Two Linux deployments and transport/recovery logic |
| Expansion | Best integrated single-system option | Best low-cost starter if no high-rate sampling | Useful for several HVAC units or remote equipment nodes |

UNO Q price changed in 2026; do not use the launch $44 price. [Arduino pricing notice](https://blog.arduino.cc/2026/06/26/a-heads-up-on-the-arduino-uno-q-board-pricing-straight-from-marcello-majonchi/). Pi Zero hardware and memory are documented on its [product page](https://www.raspberrypi.com/products/raspberry-pi-zero-2-w/). Board-level connector voltage and available functions must be checked against the exact board revision.

## What runs where

- MCU: CRC-checked temperature/RH and pressure, isolated demand pulses, dry-contact status, cached snapshots. No thermostat output, relays, or control commands.
- Linux: timestamp frames in UTC, reject malformed/out-of-order data, infer mode, calculate time-window metrics, execute persistent rules, compare reviewed baselines, write SQLite, serve dashboard.
- Optional Pi gateway: aggregate multiple UNO Q nodes over an authenticated network service. A transport extension is required; supplied code runs on either Linux host but does not implement a multi-node message broker.

The supplied server deliberately uses the Python standard library to make a reproducible offline starter. Its read-only API is isolated from hardware writes. For a deployment with multiple users, keep engine/storage separation, replace the HTTP adapter with FastAPI/Pydantic, add user authentication, HTTPS, authorization and audit logs, and migrate persistence through a tested repository layer. The SQL schema is relational and portable in concept; SQLite `PRAGMA`, `datetime()` and integer auto-IDs need PostgreSQL migrations. No PostgreSQL support is claimed as implemented.

## Supported equipment and observability

| Equipment | Useful external observations | Configuration / limits |
|---|---|---|
| Split AC | Supply/return, filter pressure, compressor/blower/fan current | Cooling only; gas heating may share the air handler |
| Heat pump | Above plus O/B, outdoor ambient, line surfaces | O/B polarity, defrost, stage and auxiliary heat required; never label defrost as cooling failure |
| Gas furnace | Temperature rise, blower, W/W2, filter | Exact furnace nameplate rise; no combustion/CO diagnosis |
| Electric furnace / air handler | Temperature rise, blower, stage currents via meter | Strip-heater stages and installed kW need exact configuration |
| Package unit | Same sensing concepts | Roof/access/weather and combined branch loads require technician work |
| Mini-split | Room/discharge temperatures, surface vibration, metered power | No assumed 24-V terminals. Inverter speed is an important unobserved confounder; conservative anomaly monitoring |

The model distinguishes commanded mode from measured compressor/fan state. Heating inferred from a W call is a demand state, not proof that a flame or heater is active. Outdoor-unit total current cannot isolate compressor versus condenser fan. Missing defrost telemetry on a heat pump reduces diagnostic confidence; disable rules that need verified operating context.

## From bench to field

1. Verify on a bench with low-voltage sensor fixtures and injected faults.
2. Commission one known installation with a technician and reference instruments.
3. Collect two to four weeks across modes/stages/weather; freeze reviewed baselines.
4. Evaluate false alerts per operating hour and fault detection delay on separate labeled data.
5. Add supervised retraining, signed updates, watchdog, retention, calibration traceability, EMC/environmental qualification and electrical certification before productization.
