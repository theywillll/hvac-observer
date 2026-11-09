// Synthetic contract tests only. No analogRead, I2C, outputs, or board upload.
#include <Arduino.h>
#include "RawBlockTests.h"
#include "RawFeatureTests.h"
#include "CurrentWindowTests.h"
#include "CurrentSnapshotTests.h"
#include "CurrentWireTests.h"
#include "CurrentUptimeTests.h"
#include "CurrentSessionTests.h"
#include <UnoQUptimeClock.h>
#include <UnoQBootToken.h>
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "Compile this self-test for arduino:zephyr:unoq"
#endif
// Reserve the proposed 800-sample/channel pool to check its target memory cost.
hvac_current::RawBlockPool<800> researchPool;
hvac_current::CurrentBootToken bootToken;
void setup() {
  Serial.begin(115200);
  const bool bootTokenOk = hvac_current::initializeUnoQBootToken(bootToken);
  Serial.print("UNO Q boot token initialization: ");
  Serial.println(bootTokenOk && bootToken.value()[0] ? "PASS" : "FAIL");
  const int sessionFailure = hvac_session_test::run();
  Serial.print("Synthetic session contract: ");
  if (sessionFailure) { Serial.print("FAIL at test line "); Serial.println(sessionFailure); }
  else Serial.println("PASS");
  const int uptimeFailure = hvac_uptime_test::run();
  Serial.print("Synthetic uptime contract: ");
  if (uptimeFailure) { Serial.print("FAIL at test line "); Serial.println(uptimeFailure); }
  else Serial.println("PASS");
  hvac_current::UnoQUptimeClock clock;
  uint64_t firstUs, secondUs;
  const bool clockOk = clock.read(firstUs) && clock.read(secondUs) && secondUs >= firstUs;
  Serial.print("UNO Q uptime read (no timing qualification): ");
  Serial.println(clockOk ? "PASS" : "FAIL");
  researchPool.reset(1, {120, 130, 20, 4095, 8}); // Synthetic limits, not qualified.
  const int failure = hvac_current_test::run();
  Serial.print("Synthetic block contract: ");
  if (failure) { Serial.print("FAIL at test line "); Serial.println(failure); }
  else Serial.println("PASS");
  const int wireFailure = hvac_wire_test::run();
  Serial.print("Synthetic bounded current wire format: ");
  if (wireFailure) { Serial.print("FAIL at test line "); Serial.println(wireFailure); }
  else Serial.println("PASS");
  const int snapshotFailure = hvac_snapshot_test::run();
  Serial.print("Synthetic current snapshot store: ");
  if (snapshotFailure) { Serial.print("FAIL at test line "); Serial.println(snapshotFailure); }
  else Serial.println("PASS");
  const int windowFailure = hvac_window_test::run();
  Serial.print("Synthetic qualified-window contract: ");
  if (windowFailure) { Serial.print("FAIL at test line "); Serial.println(windowFailure); }
  else Serial.println("PASS");
  const int featureFailure = hvac_feature_test::run();
  Serial.print("Synthetic raw-code features: ");
  if (featureFailure) { Serial.print("FAIL at test line "); Serial.println(featureFailure); }
  else Serial.println("PASS");
  Serial.print("800-sample/channel pool bytes: "); Serial.println(sizeof(researchPool));
}
void loop() { delay(1000); }
