// Standalone, blocking ADC API characterization. No RMS, I2C, or HVAC outputs.
// Compile only; any upload and protected SELV fixture testing is manual.
#include <Arduino.h>
#include <UnoQSequenceReader.h>
#include <errno.h>
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "Requires arduino:zephyr:unoq"
#endif

constexpr uint32_t REQUEST_INTERVAL_US = 1000; // Requested, never measured here.
constexpr uint32_t REQUEST_PERIOD_MS = 100;
hvac_current::RawSequence capture;
hvac_current::UnoQSequenceReader reader;
uint32_t requestNumber = 0, lastRequestMs = 0;
int setupStatus = -ENODEV;
bool halted = false;

void setup() {
  Serial.begin(115200);
  delay(1000); // Observation delay, not proof of analog stability.
  setupStatus = reader.initialize();
  Serial.print("{\"type\":\"hvac_adc_sequence\",\"version\":1,\"resolution_bits\":14,\"adc_channels\":[9,10,11],\"sweeps\":16,\"requested_interval_us\":1000,\"setup_status\":");
  Serial.print(setupStatus); Serial.println("}");
  lastRequestMs = millis();
}

void loop() {
  if (setupStatus != 0 || halted) { delay(1000); return; } // Manual reset required.
  const uint32_t now = millis();
  if (uint32_t(now - lastRequestMs) < REQUEST_PERIOD_MS) { delay(1); return; }
  lastRequestMs = now; // No catch-up burst.
  const uint32_t started = micros();
  const int status = reader.read(capture, REQUEST_INTERVAL_US);
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
