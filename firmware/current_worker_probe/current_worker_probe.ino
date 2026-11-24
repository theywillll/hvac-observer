// Standalone bench diagnostic. Compile-only development, manual board testing.
#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <UnoQBootToken.h>
#include <UnoQCaptureWorker.h>
#include <UnoQUptimeClock.h>
#include <SequenceFeatures.h>
#include <CurrentSessionCommand.h>

constexpr uint32_t CONSUMER_PAUSE_MS = 0; // Use 250 to exercise backpressure.

using hvac_current::CapturePacket;
using hvac_current::CapturePacketKind;
hvac_current::UnoQCaptureWorker worker;
hvac_current::CurrentBootToken bootToken;
hvac_current::CurrentSessionStore sessions(6000000);
hvac_current::UnoQUptimeClock ownerClock;
bool readerReady = false;

hvac_current::WindowPolicy handshakePolicy() {
  hvac_current::WindowPolicy policy;
  policy.samples = hvac_current::RawSequence::Sweeps;
  policy.minIntervalUs = 990;
  policy.maxIntervalUs = 1010;
  policy.maxAgeUs = 1000000;
  policy.codeMax = hvac_current::RawSequence::CodeMax;
  return policy;
}

String getWorkerBootToken() { return String(bootToken.value()); }

String handleWorkerSession(String request) {
  if (!readerReady) return String();
  uint64_t nowUs;
  if (!ownerClock.read(nowUs)) {
    sessions.clockFailed();
    return String();
  }
  char reply[hvac_current::CurrentSessionCommandCapacity];
  if (!hvac_current::applyCurrentSessionCommand(
          sessions, bootToken.value(), request.c_str(), request.length(),
          handshakePolicy(), nowUs, reply, sizeof(reply))) return String();
  return String(reply);
}

void dispatchCapture() {
  if (!readerReady) return;
  uint64_t nowUs;
  if (!ownerClock.read(nowUs)) {
    sessions.clockFailed();
    return;
  }
  hvac_current::CurrentCaptureTicket ticket;
  if (!sessions.beginCapture(nowUs, ticket)) return;
  if (!worker.submit(ticket)) {
    sessions.stop();
    Serial.println("{\"dispatch_failed\":true}");
  }
}

void printU64(uint64_t value) {
  char text[21];
  snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
  Serial.print(text);
}

void printFeatureNumber(double value) {
  if (isfinite(value)) Serial.print(value, 6);
  else Serial.print("null");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  hvac_current::initializeUnoQBootToken(bootToken);
  Bridge.begin();
  Bridge.provide_safe("hvac_current_boot", getWorkerBootToken);
  Bridge.provide_safe("hvac_current_session", handleWorkerSession);
  Serial.println("{\"type\":\"hvac_adc_worker\",\"version\":2,\"resolution_bits\":14,\"adc_channels\":[9,10,11],\"sweeps\":16,\"requested_interval_us\":1000,\"queue_depth\":2,\"request_depth\":1}");
  worker.begin();
}

void loop() {
  uint64_t nowUs;
  if (ownerClock.read(nowUs)) sessions.expire(nowUs);
  else sessions.clockFailed();
  CapturePacket packet;
  if (!worker.poll(packet)) {
    dispatchCapture();
    delay(1);
    return;
  }
  if (packet.kind == CapturePacketKind::Setup) {
    readerReady = packet.status == 0;
    Serial.print("{\"setup_status\":"); Serial.print(packet.status); Serial.println("}");
    return;
  }
  if (packet.kind == CapturePacketKind::Terminal) {
    readerReady = false;
    sessions.stop();
    Serial.print("{\"terminal_reason\":"); Serial.print(packet.status);
    Serial.println("}");
    return;
  }

  // Re-read the owner clock after receiving the completion. Session changes,
  // expiry and ticket consumption share the sketch's safe callback context.
  const bool ownerClockOk = ownerClock.read(nowUs);
  if (!ownerClockOk || !packet.clockOk) sessions.clockFailed();
  const bool accepted = ownerClockOk && packet.clockOk && sessions.finishCapture(packet.ticket, nowUs);
  Serial.print("{\"request\":"); printU64(packet.request);
  Serial.print(",\"session\":\""); printU64(packet.ticket.session); Serial.print('"');
  Serial.print(",\"capture_ordinal\":\""); printU64(packet.ticket.ordinal); Serial.print('"');
  Serial.print(",\"accepted\":"); Serial.print(accepted ? "true" : "false");
  Serial.print(",\"clock_ok\":"); Serial.print(packet.clockOk ? "true" : "false");
  Serial.print(",\"started_us\":"); printU64(packet.startedUs);
  Serial.print(",\"returned_us\":"); printU64(packet.returnedUs);
  Serial.print(",\"driver_status\":"); Serial.print(packet.status);
  Serial.print(",\"guard_ok\":"); Serial.print(packet.raw.guardsOk() ? "true" : "false");
  Serial.print(",\"channels\":[");
  hvac_current::RawFeatures channels[3];
  hvac_current::sequenceFeatures(packet.raw, accepted ? packet.status : -EIO, channels);
  for (unsigned channel = 0; channel < 3; ++channel) {
    const auto& features = channels[channel];
    if (channel) Serial.print(',');
    Serial.print("{\"channel\":"); Serial.print(channel);
    Serial.print(",\"faults\":"); Serial.print(features.faults);
    Serial.print(",\"samples\":"); Serial.print(features.sampleCount);
    Serial.print(",\"near_rail\":"); Serial.print(features.nearRailCount);
    Serial.print(",\"mean_code\":"); printFeatureNumber(features.meanCode);
    Serial.print(",\"ac_rms_code\":"); printFeatureNumber(features.acRmsCode);
    Serial.print('}');
  }
  Serial.println("]}");
  delay(CONSUMER_PAUSE_MS);
  dispatchCapture();
}
