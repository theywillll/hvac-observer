# HVAC Observer

HVAC Observer watches sensor readings from a heating or cooling system and flags patterns worth checking: a rising filter pressure drop, short cycling, an unusual temperature split, or equipment that doesn't respond to a call. It records the evidence behind each alert so you can see what triggered it.

The project runs locally with Python, SQLite and a browser dashboard. Start with the simulator; no hardware or extra Python packages are needed. The hardware path targets Arduino UNO Q, with a Raspberry Pi option for temperature and humidity sensing. It only monitors equipment and has no control outputs.

## Try it

Use Python 3.11 or newer. From the repository root:

```sh
python -m backend.server --scenario dirty_filter
```

Open [localhost:8765](http://127.0.0.1:8765). The demo runs at 20 times normal speed, and the fault starts after ten simulated minutes. The dashboard labels the readings as synthetic. Use `--speed 1` to watch it in real time; Ctrl+C stops the server.

Try `normal_cooling` or `normal_heating` for a healthy run. Other scenarios include `frozen_evaporator`, `short_cycling`, `failed_condenser_fan`, `blower_degradation`, `compressor_current_anomaly`, `no_response`, `humidity_degradation` and `condensate`.

To run the tests:

```sh
python -m unittest discover -s tests -v
```

## How it works

Sensor frames pass through validation and calibration before the service infers operating mode and evaluates the rules. Alerts use persistence windows to avoid reacting to a single reading. A commissioned baseline can also flag changes relative to healthy operation under similar conditions.

SQLite keeps measurements, operating state and alerts. The dashboard shows live values, eight trend views and the readings behind each alert. The equipment catalog contains 17 reference profiles across 14 manufacturers, with sources attached to individual parameters. Unknown values stay `null`, and selecting a profile doesn't apply its limits to an installation.

## Hardware

UNO Q puts the sensor MCU and Linux service on one board. The sketch reads two SHT31 temperature/humidity sensors and an SDP810 pressure sensor. Optional isolated thermostat inputs and an independent condensate float are disabled until commissioning. The Pi adapter reads the two SHT31s.

Current, vibration, remote temperature probes and power metering need additional acquisition drivers. Their interfaces and reference circuits are documented, but those drivers aren't implemented in the sketch.

The [BOM](hardware/bom.csv) estimates about $169 for basic thermal/humidity monitoring, $375 with pressure and control inputs, and $1,300+ for expanded instrumentation. These are planning figures in USD, excluding tax and labor. See the [platform comparison](docs/architecture.md) for the board choice and pricing sources.

Read the [safety notes](docs/safety.md) before connecting hardware. Field wiring, duct taps and current/power sensing need a qualified installer.

## Project status

The simulator, service, database and dashboard data endpoints have automated test coverage. The 26-test suite passed locally and in GitHub Actions. JavaScript syntax was checked; visual browser testing is still outstanding.

The UNO Q sketch hasn't been compiled or tested on a board, and no real HVAC installation has been validated. The diagrams are reference designs, not PCB fabrication files. Synthetic fault detections show how the rules behave; they don't establish diagnostic accuracy in the field.

The server listens on loopback for local use. Alerts are shown in the dashboard and saved in the database; there are no outbound notifications. It doesn't estimate refrigerant charge, SEER or COP, and it can't assess combustion safety, CO or refrigerant leaks.

See the [validation record](docs/validation.md) for completed checks and the remaining bench work.

## Find your way around

| Path | Contents |
|---|---|
| `backend/` | Acquisition adapters, validation, rules, baselines, storage and HTTP service |
| `dashboard/` | HTML, CSS and JavaScript; all assets are local |
| `firmware/` | UNO Q sketch and board setup notes |
| `hardware/` | Reference diagrams, pin map, sensors and BOM |
| `data/` | Equipment profiles, citations, schema and sample readings |
| `models/` | Synthetic example baseline and commissioning notes |
| `scripts/` | Database import, simulation, export, commissioning and retention tools |
| `tests/` | Rule tests and HTTP/SQLite integration tests |

- [Setup and commissioning](docs/installation.md)
- [Diagnostic rules and false positives](docs/diagnostics.md)
- [API and sensor frame format](docs/api.md)
- [Equipment data and source notes](data/README.md)
- [Hardware drawings](hardware/README.md)
- [Editable KiCad schematics](hardware/kicad/README.md)
- [Contributing](CONTRIBUTING.md)

Licensed under [MIT](LICENSE).
