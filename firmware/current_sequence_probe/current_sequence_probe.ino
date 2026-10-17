// Standalone, blocking ADC API characterization. No RMS, I2C, or HVAC outputs.
// Compile only; any upload and protected SELV fixture testing is manual.
#include <Arduino.h>
#include <zephyr/drivers/adc.h>
#include <zephyrPinctrl.h>
#include <RawSequence.h>
#include <errno.h>
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "Requires arduino:zephyr:unoq"
#endif

static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 0) == 9, "A0 mapping changed");
static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 1) == 10, "A1 mapping changed");
static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 2) == 11, "A2 mapping changed");
const adc_dt_spec INPUTS[] = {
  ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
  ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
  ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2)
};
constexpr uint32_t REQUEST_INTERVAL_US = 1000; // Requested, never measured here.
constexpr uint32_t REQUEST_PERIOD_MS = 100;
hvac_current::RawSequence capture;
uint32_t requestNumber = 0, lastRequestMs = 0;
int setupStatus = -ENODEV;
bool halted = false;

int configureInputs() {
  for (size_t i = 0; i < 3; ++i) {
    if (INPUTS[i].dev != INPUTS[0].dev || INPUTS[i].channel_id != 9 + i ||
        INPUTS[i].resolution != 14 || INPUTS[i].oversampling != INPUTS[0].oversampling ||
        INPUTS[i].channel_cfg.differential) return -ENOTSUP;
    // Initialize ADC and restore each requested analog pin individually.
    int status = zephyr::arduino::init_dev_apply_channel_pinctrl(
      INPUTS[i].dev, zephyr::arduino::state_pin_index_from_spec_index(INPUTS, i));
    if (status != 0) return status;
    if (!adc_is_ready_dt(&INPUTS[i])) return -ENODEV;
    status = adc_channel_setup_dt(&INPUTS[i]);
    if (status != 0) return status;
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Observation delay, not proof of analog stability.
  setupStatus = configureInputs();
  Serial.print("{\"type\":\"hvac_adc_sequence\",\"version\":1,\"resolution_bits\":14,\"adc_channels\":[9,10,11],\"sweeps\":16,\"requested_interval_us\":1000,\"setup_status\":");
  Serial.print(setupStatus); Serial.println("}");
  lastRequestMs = millis();
}

void loop() {
  if (setupStatus != 0 || halted) { delay(1000); return; } // Manual reset required.
  const uint32_t now = millis();
  if (uint32_t(now - lastRequestMs) < REQUEST_PERIOD_MS) { delay(1); return; }
  lastRequestMs = now; // No catch-up burst.
  capture.prepare();
  adc_sequence_options options{};
  options.interval_us = REQUEST_INTERVAL_US;
  options.extra_samplings = hvac_current::RawSequence::Sweeps - 1;
  adc_sequence sequence{};
  sequence.options = &options;
  sequence.channels = BIT(9) | BIT(10) | BIT(11);
  sequence.buffer = capture.codes;
  sequence.buffer_size = sizeof(capture.codes);
  sequence.resolution = INPUTS[0].resolution;
  sequence.oversampling = INPUTS[0].oversampling;
  const uint32_t started = micros();
  const int status = adc_read(INPUTS[0].dev, &sequence);
  const uint32_t elapsed = uint32_t(micros() - started);
  halted = !capture.guardsOk(); // Report once, then stop after guard damage.
  // The one bracket covers the whole request. No sample-aperture timestamps.
  Serial.print("{\"request\":"); Serial.print(requestNumber++);
  Serial.print(",\"start_us\":"); Serial.print(started);
  Serial.print(",\"read_us\":"); Serial.print(elapsed);
  Serial.print(",\"driver_status\":"); Serial.print(status);
  Serial.print(",\"guard_ok\":"); Serial.print(capture.guardsOk() ? "true" : "false");
  Serial.print(",\"raw\":");
  if (!capture.publishable(status)) Serial.print("null");
  else {
    Serial.print('[');
    for (size_t i = 0; i < hvac_current::RawSequence::Count; ++i) {
      if (i) Serial.print(',');
      Serial.print(capture.codes[i]);
    }
    Serial.print(']');
  }
  Serial.println("}");
}
