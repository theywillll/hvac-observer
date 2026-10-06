# Equipment reference data

`equipment_profiles.json` contains 17 seed profiles across Carrier, Trane, Lennox, Rheem, Goodman, Amana, American Standard, Bryant, York, Coleman, Daikin, Mitsubishi Electric, Fujitsu and Bosch. The dataset includes older equipment that may still be in service. It is not a current sales catalog or coverage of every model.

Most records are **family** references. Rheem includes a named SKU; Fujitsu includes a system designation; Mitsubishi is explicitly an indoor-only record. Some identity-only records are retained to show unknown parameters, not counted as verified operating envelopes. Rated current, static limits, exact airflow, furnace temperature rise and fault-code dictionaries remain unknown where no exact applicable document was verified. Do not infer null as zero, normal, or unrestricted.

Every non-null manufacturer parameter has `source_id`, `unit`, `provenance`, and `qualifier`. The source ID resolves through `sources.json` to URL, document, version/date if available, access date and locator. `equipment_parameters.csv` is the flattened audit/export view. No copyrighted manuals are redistributed; links and limited factual extracts are provided.

Provenance taxonomy: `manufacturer_specification`, `industry_reference_guideline`, `prototype_heuristic`, `learned_baseline`, `derived_parameter`, and `unknown`. A manufacturer's family maximum SEER2 is not an alarm setting; SEER and SEER2 remain separate fields. Refrigerant and control revisions must not be transferred across generations. A null source on a heuristic means it is this prototype's design choice, not a falsely attributed industry limit.

Detected source conflicts are retained in notes. York's YC2F live page mixes SEER and SEER2 statements; no numeric efficiency was accepted. Lennox's live marketing page conflicts with its older ML14XC1 bulletin; this record explicitly uses the June 2023 single-phase R-410A bulletin. Do not merge these documents without exact revision matching.

To add equipment: obtain the exact manufacturer submittal/manual; record indoor/outdoor match, voltage/frequency/phase, serial/revision applicability, source publication version and a stable page/table locator. Add parameter values with explicit qualifiers. An efficiency rating requires the applicable matched-system certification; a furnace rise range requires the exact unit label/manual. Add tests for provenance and unknown behavior, then run `python -m scripts.init_db hvac.sqlite3` to import.

The runtime configuration deliberately requires installer verification before a profile limit controls an alert. The profile viewer retrieves references; it does not silently apply family limits. This keeps reference browsing separate from installation settings. Unknown-equipment mode uses explicit broad control configuration, nullable limits and reviewed learning.

Schema tables: equipment, sensors, sessions, measurements, hvac_state, alerts, baselines, sources and profile_parameters. Foreign keys bind installation measurements and sessions. Time-indexed observations retain raw/calibrated values and quality; runtime state payload includes metrics and active alerts. `baselines` supports future database-managed versioning; the implemented commissioning workflow writes versioned JSON models and loads them explicitly.
