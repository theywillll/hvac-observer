# Baseline models

`demo_baseline.json` is synthetic and only for reproducing the simulator. It is not a pretrained HVAC fault classifier. Hardware mode rejects models marked synthetic.

Create an installation baseline with `scripts.commission` from reviewed healthy data. Models contain median, robust scale, sample count and environmental bucket, plus learning period and schema version. Keep original models after maintenance and save replacements under a new name. No automatic retraining or cloud transfer is implemented.

Measure field precision, recall, false alerts per operating hour and detection delay before calibrating confidence as probability. Separate evaluation by installation and time; random splitting adjacent correlated samples leaks information. Include healthy extreme weather, defrost, filter changes, off-cycle fans, variable stages, sensor dropouts and resets. A model that scores synthetic examples well is not established as a field diagnostic tool.
