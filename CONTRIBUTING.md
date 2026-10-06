# Contributing

Run the tests from the repository root:

```sh
python -m unittest discover -s tests -v
```

For a new diagnostic rule, include the readings that should trigger it and the healthy conditions that might look similar. Cover persistence, missing sensors and mode changes. Explain where the threshold comes from and what could cause a false alert. Keep tuning data separate from evaluation data, including by installation and time period.

Equipment data needs a source for each manufacturer parameter. Use the exact model and document revision where possible, and leave unsupported values unknown. A family efficiency rating isn't an installed operating limit.

Hardware changes need a schematic review, calibration against reference instruments and bench results before field testing. Keep this project passive: no automatic actuation or unisolated connections to mains or control boards.

Before a release, record the Python and board/core versions, check the dashboard at narrow and wide widths, and test database migration, backup and recovery. Review dependency licenses and the safety notes. Internet access and external notifications need an authentication and security design before they're added.
