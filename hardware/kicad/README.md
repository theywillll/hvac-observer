# KiCad sensing interface

Open `hvac-observer.kicad_pro` in KiCad 10, then open the schematic editor. Keep all files in this directory together: the root schematic references three child sheets and the project symbol library.

The project has a cover sheet and three circuit sheets:

| Sheet | Contents | Preview |
|---|---|---|
| Sensors | UNO Q interface harness, sensor breakout connectors, I2C pullups, decoupling and independent float input | [View](previews/hvac-observer-Sensors%20and%20MCU%20interface.svg) |
| Thermostat inputs | Seven H11AA1 channels for R, Y, Y2, W, W2, G and O/B, with fuses, LED resistors, pullups and output filters | [View](previews/hvac-observer-Isolated%20thermostat%20inputs.svg) |
| Current inputs | Three voltage-output CT interfaces, mid-rail buffer, RC filters and rail clamps | [View](previews/hvac-observer-Optional%20current%20inputs.svg) |

`J1` is a 15-way **harness definition**, not an UNO Q header footprint. Its net names map to the board signals in [pinout.csv](../pinout.csv). The sensor connectors also define a harness order; check the actual breakout pin order before making cables. SHT31 supply address 0x45 requires its module address strap to be set high.

Global net names connect the sheets. `HVAC_C` is the field-side AC reference. `GND_SELV` is electronics ground. They have no copper connection in this design. The board uses its separate approved USB-C supply; the drawing accepts an approved 3.3V sensor rail and does not include a regulator or derive power from HVAC R/C.

The optional current sheet uses voltage-output CTs with internal burdens. R209 isolates the bias reservoir capacitance from the MCP6002 follower output; stability and transient response still need a bench check. BAS70 clamp leakage, ADC limits and injected rail current need checking on the selected hardware. The environmental firmware does not implement waveform acquisition.

## Checks and exports

KiCad 10.0.6 loaded the hierarchy and exported all four SVG sheets and the netlist. Its electrical rules check reported **zero errors and zero warnings**, recorded in [erc-report.json](erc-report.json). The exported netlist also passed 108 connection checks covering MCU mappings, sensor harnesses, optocoupler connections, clamp polarity and field/SELV separation; see [connectivity-check.json](connectivity-check.json).

The checks establish the drawn connections. They do not establish isolation safety, analog performance or suitability for installation. No SPICE simulation, PCB layout or hardware test has been performed. All four rendered sheets were visually reviewed.

To repeat the exports from this directory:

```sh
kicad-cli sch erc --format json --exit-code-violations -o erc-report.json hvac-observer.kicad_sch
kicad-cli sch export netlist -o hvac-observer.net hvac-observer.kicad_sch
kicad-cli sch export svg -o previews/ hvac-observer.kicad_sch
```

[schematic-bom.csv](schematic-bom.csv) lists 95 component references. This is a schematic inventory, not a purchasing list: connectors, fuse holders, voltage ratings, tolerances and footprints still need selection. The wider [project BOM](../bom.csv) covers boards and instrumentation.

## Before building

Review component ratings, fuse coordination, transformer loading and optocoupler detection over voltage and temperature. Select footprints and verify clearances and enclosure requirements before routing a PCB. Start with a protected low-voltage bench fixture; field connections require a qualified installer. See [hardware notes](../README.md) and [safety](../../docs/safety.md).

## Symbol sources

The project library contains symbols from the KiCad 10.0.6 distribution's `Device`, `Connector_Generic`, `Isolator`, `Amplifier_Operational` and `power` libraries, maintained by the KiCad community. MCP6002 inheritance was flattened for a self-contained library; schematic instances use the local `HVAC` library name. Symbol definitions retain their original pin numbers.

The redistributed symbol library is covered by [KiCad's CC-BY-SA 4.0 library license and design exception](LICENSE-symbols.txt), separately from the project's MIT license. See the [KiCad license page](https://www.kicad.org/libraries/license/).

Pinout references: [Vishay H11AA1](https://www.vishay.com/docs/83608/h11aa1.pdf), [Microchip MCP6002](https://www.microchip.com/en-us/product/mcp6002). Electrical values and commissioning limits follow the [hardware reference](../README.md).
