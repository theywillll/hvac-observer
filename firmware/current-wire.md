# Nullable current wire contract, schema 1

The bounded C++ writer in `HvacCurrent/src/CurrentWire.h` and the Python
validator in `backend/current_quality.py` now agree on a current payload.
Synthetic fixtures exercise their actual serialized JSON end to end. This is
**not a connected ADC/RouterBridge acquisition path**: the environmental sketch
is unchanged and still publishes no current estimates. No timing/calibration
record has been qualified on hardware.

## Payload and channel mapping

The writer returns a standalone JSON object containing exactly three current
fields plus `current_quality`. A future adapter must merge that object into the
normal acquisition frame, preserving its quality object and adding the usual
`ts`, `seq` and `source`. The serializer supplies neither a wall-clock timestamp
nor an environmental frame sequence.

| Logical channel | Measurement field | Existing validated range |
| --- | --- | --- |
| 0, compressor | `compressor_a` | 0..200 A RMS |
| 1, blower | `blower_a` | 0..100 A RMS |
| 2, condenser fan | `condenser_fan_a` | 0..30 A RMS |

These are wire sanity bounds, not CT ratings or measured operating limits. A
gate-qualified result outside a wire range fails serialization; discard the
whole payload. Do not clamp it into range or reuse the last good JSON string.

Example **synthetic** channel 0, with the other channels not yet observed:

```json
{
  "compressor_a": 1,
  "blower_a": null,
  "condenser_fan_a": null,
  "current_quality": {
    "schema": 1,
    "session": "1",
    "max_age_us": 5000,
    "channels": [
      {"channel": 0, "available": true, "faults": 0, "age_us": 1000,
       "window_seq": "0", "timing_evidence_id": 7,
       "calibration_id": 9, "calibration_revision": 2},
      {"channel": 1, "available": false, "faults": 2048, "age_us": null,
       "window_seq": null, "timing_evidence_id": null,
       "calibration_id": null, "calibration_revision": null},
      {"channel": 2, "available": false, "faults": 2048, "age_us": null,
       "window_seq": null, "timing_evidence_id": null,
       "calibration_id": null, "calibration_revision": null}
    ]
  }
}
```

All values, gains, evidence IDs and age limits above are test fixtures. They do
not constitute a selected production sampling policy or a board measurement.

| Quality field | Type and meaning |
| --- | --- |
| `schema` | Integer 1; bool and other schemas rejected |
| `session` | Canonical nonzero uint64 decimal **string**, or null before configuration |
| `max_age_us` | Positive integer below 2^31 for configured sessions; zero only with null session |
| `channels` | Exactly three objects ordered 0, 1, 2; unknown fields rejected |
| `available` | Boolean; true requires a numeric measurement and all qualification fields |
| `faults` | Integer bit mask below 2^14; zero only for an available result |
| `age_us` | Sample-end to snapshot-request age, 0..2^32-2, or null if unknown |
| `window_seq` | Canonical uint64 decimal string, including "0", or null if unobserved |
| `timing_evidence_id` | Nonzero uint32 reference, or null when absent |
| `calibration_id`, `calibration_revision` | Paired nonzero uint32 references, or both null |

Strings avoid JavaScript precision loss for 64-bit identities. Leading zeros,
signs, whitespace, non-ASCII digits and overflow are rejected. A null age is
unknown, never zero. The internal `UINT32_MAX` age sentinel is serialized as null.
The snapshot now explicitly carries whether each channel has observed a window;
window sequence zero is not confused with an unobserved channel.

An available reading requires zero faults, nonnull session/window/evidence and
calibration references, a known age at or below the policy limit, and a finite
numeric measurement in range. An unavailable reading requires a nonzero fault
mask and a null measurement. Unobserved channels have no window metadata and
carry a configuration or pending fault. An observed expired age requires the
age fault. Python returns an independent copy of validated quality metadata.

## Fault mask

Masks can combine reasons. They describe data validity, not a diagnosed HVAC fault.

| Bit value | Name | Meaning |
| --- | --- | --- |
| 1 | `WindowConfig` | No valid configuration |
| 2 | `WindowSession` | Wrong session |
| 4 | `WindowChannel` | Invalid channel |
| 8 | `WindowSequence` | Duplicate/backward/exhausted window identity |
| 16 | `WindowGap` | Missing window ordinal |
| 32 | `WindowFeatures` | Invalid raw features, count, scale or clipping |
| 64 | `WindowTiming` | Unqualified/inconsistent sample timing |
| 128 | `WindowAge` | Expired/future sample end |
| 256 | `WindowCalibration` | Missing/unapproved calibration |
| 512 | `WindowRange` | Outside the approved RMS-code calibration range |
| 1024 | `WindowNumeric` | Invalid conversion arithmetic |
| 2048 | `WindowPending` | Configured, no observed window |
| 4096 | `WindowStopped` | Store stopped |
| 8192 | `WindowClock` | Extended clock regressed |

The store ignores wrong-session/channel publications before replacing a result;
those gate-level bits are reserved in the shared mask but are not used to
invalidate another active channel through that publication path.

## Bounded serialization and calibration ownership

`writeCurrentWire(store, nowUs, output, capacity)` requests a fresh snapshot
internally and requires the same external serialization as all store operations.
It does not accept a cached snapshot. `CurrentWireCapacity` is 1,536 bytes.
Serialization allocates no dynamic storage. A successful write is NUL-terminated;
any capacity, range, locale or formatting failure returns false and clears the
output when a nonnull positive-capacity buffer was supplied. No partial JSON is
usable. Zero capacity/null pointers fail without access. Age checks may still
have invalidated stored readings before a later formatting failure.

Numeric output uses 17 significant digits; nonfinite values are never emitted.
A non-C locale decimal separator fails closed. Tests exercise every undersized
capacity for a representative populated frame, exact-fit success, canaries,
full-width identity/metadata values and extra room for three numeric fields.
That buffer is a local serialization bound, **not a verified Bridge message size**.

Schema 1 current values have already passed the MCU's conditional measured-chain
conversion. Server calibration entries for **any** of these three fields are
therefore rejected whenever `current_quality` is present, even if that channel
is currently null or the configured gain is one. Other sensor calibration works
as before. Remove those server entries instead of silently applying a second
gain/offset while retaining the original MCU calibration reference.

## Compatibility and limits of validation

Hardware frames containing any numeric current now require `current_quality`.
Older temperature-only UNO/Pi frames with absent/all-null currents remain
accepted. Explicit `simulator` and `replay` frames can retain their legacy
numeric fields without quality metadata; they are not thereby qualified hardware.
If metadata is supplied, it is checked consistently for every source. Existing
JSONL hardware producers must migrate rather than relabel hardware as simulation.

Quality survives engine processing and SQLite state-history JSON. It does not
claim a trusted calibration registry: identifiers and qualified flags originate
in trusted producer code. Wire consistency checks cannot prove their truth,
sensor connectivity, motor operation, isolation, accuracy or safety. Measurement
table quality labels still conservatively say `unverified_calibration` unless
the existing installer-calibration path supplies metadata; full current provenance
is in each state payload's `values.current_quality`.

The receiving validator is stateless regarding current sessions/window ordinals.
It checks the declared age **at snapshot request**, not transport delay or future
display/use age. Existing normal-frame sequence checks remain in the engine.
Replay protection, boot identity, transport deadlines and expiry while a consumer
retains a reading still need integration. The dashboard's existing 15-second
whole-frame stale flag is not a current-window freshness guarantee.

The [UNO receiver increment](current-receiver.md) now adds process-local
frame/window replay checks and conservative host receipt deadlines, conditional
on fresh request-time production. Source age metadata remains unchanged. Boot
identity, cancellation/physical transport qualification and consumer-use expiry
are still pending; the serializer is not connected to the MCU callback.

The subsequent [live-consumer layer](current-live.md) now carries receipt
deadlines to ingestion/status and adds browser expiry/error handling while
preserving history. Physical qualification, persistent identity and a connected
MCU current producer remain pending.

No RouterBridge size or round-trip claim is made. The pinned local RPClite 0.3.0
source defines `DECODER_BUFFER_SIZE` as 1,024 and its default request buffer as
one quarter of that (`Arduino_RPClite.h`, `request.h`). Those constants alone do
not establish response-string capacity once the environmental payload and RPC
envelope are added. A transport design must inspect both directions and test
bounds before connecting this writer. Do not merely raise a buffer and assume
the receiving side matches.

## Reproduction and next increment

```sh
python -m unittest discover -s tests -v
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Ifirmware/libraries/HvacCurrent/src tests/current_block_test.cpp -o /tmp/current-block-test
/tmp/current-block-test
/tmp/current-block-test --current-wire-fixtures | python3 -m scripts.check_current_wire
```

The fixture stream is explicitly synthetic. It covers unconfigured/pending,
numeric and genuine synthetic zero, decimal precision, inclusive freshness,
expiry/full-cycle age, stopped/reset/future/clock failures and maximum metadata.
CI uses the real Python validator and compiles the shared self-test on UNO Q.
Only native execution and target compilation have occurred; no board upload.
See [wire-build-record.json](wire-build-record.json) for the local evidence.

Next: choose and test a bounded transport envelope, consumer-age/replay rules and
session/clock ownership, then connect a synchronized worker behind unqualified
defaults. Keep waveform work separate from slow I2C and RPC. Actual sample
aperture timing, anti-aliasing, CT calibration and accuracy require protected
fixtures, a board and reference measurements before current can be enabled.
