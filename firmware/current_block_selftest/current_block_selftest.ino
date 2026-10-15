// Synthetic contract tests only. No analogRead, I2C, outputs, or board upload.
#include <Arduino.h>
#include "RawBlockTests.h"
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "Compile this self-test for arduino:zephyr:unoq"
#endif
// Reserve the proposed 800-sample/channel pool to check its target memory cost.
hvac_current::RawBlockPool<800> researchPool;
void setup() {
  Serial.begin(115200);
  researchPool.reset(1, {120, 130, 20, 4095, 8}); // Synthetic limits, not qualified.
  const int failure = hvac_current_test::run();
  Serial.print("Synthetic block contract: ");
  if (failure) { Serial.print("FAIL at test line "); Serial.println(failure); }
  else Serial.println("PASS");
  Serial.print("800-sample/channel pool bytes: "); Serial.println(sizeof(researchPool));
}
void loop() { delay(1000); }
