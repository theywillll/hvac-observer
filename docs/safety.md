# Safety and installation boundaries

**Passive measurement only. This prototype is not a thermostat, protective interlock, smoke/CO detector, refrigerant detector, electrical protective device or a substitute for manufacturer service procedures.** No automatic equipment shutdown is wired. Critical alerts advise action through normal controls; factory safeties remain independent.

| Work | Responsible party / boundary |
|---|---|
| Run simulator; assemble isolated sensor bench | Student / developer |
| Place room/outdoor probes with no equipment opening | User, following mounting/weather guidance |
| Duct penetrations, coil/line probes, drain switch placement | Qualified HVAC technician; locate coil, wiring, moving parts and refrigerant tubes before any penetration |
| Attach thermostat sense leads | Technician verifies conventional 24 VAC, transformer loading, fusing, leakage and no control-board modification |
| Open 120/240-V compartment or install CT/meter | Qualified electrician/HVAC technician with equipment isolated, absence of voltage verified and capacitors treated as hazardous |
| Refrigerant pressure/charge work | Appropriately qualified/certified technician; excluded from beginner build |

Do not directly connect mains or R/C/Y/W/G/O-B terminals to GPIO, USB ground or ADC. The thermostat C wire is the HVAC transformer return, **not electronics ground**. A mistaken short can damage the control transformer or start equipment. Independent transformer systems (Rc/Rh) must not be bridged. Optocouplers must preserve physical separation as well as electrical isolation.

Use an approved isolated USB supply plugged into a normal outlet. Do not parasitically power this build from the furnace transformer. Keep the low-voltage electronics enclosure outside the mains compartment. Supply and signal grounds are common only within the SELV electronics domain. Neither a grounded laptop nor a USB cable may bypass the input isolation barrier.

A CT senses one conductor, not a whole multi-conductor cable. Installing it can still require hazardous compartment access. The specified voltage-output CT contains a burden; do not substitute a current-output CT with the same-looking connector. **A current-output CT must never have an open secondary while its primary is energized.** Use an appropriate shorting terminal procedure and rated burden under professional supervision. Do not modify a CT's insulation. CTs measure AC; inverter internals can require different probes and expertise. [Magnelab specified voltage-output device](https://magnelab.com/product/sct-0750/)

No homemade mains voltage divider is provided. Voltage and real power must come from a professionally installed, jurisdiction-appropriate meter with its own installation protection. A networking interface is not permission to bring meter line terminals into a hobby enclosure. Use the manufacturer's exact wiring for US split-phase systems; never blindly sum or double a single current channel. [Meter documentation](https://us.shelly.com/blogs/documentation/shelly-pro-3em)

Refrigerants involve high pressure, frostbite, asphyxiation and, for some refrigerants, flammability risks. The prototype never opens a refrigerant circuit. US servicing requirements include applicable EPA Section 608 certification; local licensing requirements are separate. [EPA certification information](https://www.epa.gov/section608/section-608-technician-certification-0). Do not add refrigerant based on this software's inferred symptoms.

Do not attach this project's float to a factory safety circuit or bypass a drain interlock. Use an independent float/contact and verify mechanical placement. A normally-open contact cannot distinguish a broken wire from a dry pan; a commercial upgrade needs supervised end-of-line wiring. Inspect drain sensors regularly.

No alarm should be interpreted as proof of safe operation. On signs of smoke, burning, gas odor or a CO alarm, follow the equipment/emergency instructions and leave diagnosis to appropriate professionals. Keep independent listed smoke/CO alarms in service.
