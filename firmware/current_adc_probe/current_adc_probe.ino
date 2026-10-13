/* UNO Q bench probe. Raw ADC codes and call timing only, never RMS amperes.
 * Connect only reviewed 0-3.3 V SELV fixtures to A0-A2 and electronics ground.
 * No CT installation, upload or physical validation has been performed.
 */
#include <Arduino.h>
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "This bench probe requires arduino:zephyr:unoq"
#endif

constexpr uint8_t ADC_BITS = 12;
constexpr int CODE_MAX = (1 << ADC_BITS) - 1;
constexpr int RAIL_MARGIN_CODES = 8; // Observation flag, not a protection limit.
constexpr uint32_t SWEEP_PERIOD_MS = 100;
const pin_size_t INPUT_PINS[] = {A0, A1, A2};

struct Reading {
  uint32_t startUs;
  uint32_t elapsedUs;
  int raw; // Keep negative core/driver errors; never cast to an unsigned code.
};
uint32_t sweep = 0;
uint32_t lastSweepMs = 0;

const char* statusOf(int raw) {
  if (raw < 0) return "driver_error";
  if (raw > CODE_MAX) return "invalid_code";
  if (raw <= RAIL_MARGIN_CODES || raw >= CODE_MAX - RAIL_MARGIN_CODES)
    return "near_rail";
  return "ok";
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(ADC_BITS);
  // analogRead configures analog input mode. No output pins or HVAC commands.
  delay(1000); // Startup observation delay, not proof of bias stability.
  Serial.println("# hvac_adc_probe; bits=12; sweep_ms=100; raw_only");
  Serial.println("sweep,channel,start_us,read_us,raw,status");
  lastSweepMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (uint32_t(now - lastSweepMs) < SWEEP_PERIOD_MS) {
    delay(1);
    return;
  }
  lastSweepMs = now; // No catch-up burst after a stall.
  Reading readings[3];
  for (uint8_t channel = 0; channel < 3; ++channel) {
    readings[channel].startUs = micros();
    readings[channel].raw = analogRead(INPUT_PINS[channel]);
    readings[channel].elapsedUs = uint32_t(micros() - readings[channel].startUs);
  }
  // Print only after all three calls, preserving each channel's own bracket.
  for (uint8_t channel = 0; channel < 3; ++channel) {
    Serial.print(sweep); Serial.print(',');
    Serial.print(channel); Serial.print(',');
    Serial.print(readings[channel].startUs); Serial.print(',');
    Serial.print(readings[channel].elapsedUs); Serial.print(',');
    Serial.print(readings[channel].raw); Serial.print(',');
    Serial.println(statusOf(readings[channel].raw));
  }
  ++sweep;
}
