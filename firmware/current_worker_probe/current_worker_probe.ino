// Standalone bench diagnostic. Compile-only development, manual board testing.
#include <Arduino.h>
#include <UnoQSequenceReader.h>
#include <UnoQUptimeClock.h>
#include <SequenceFeatures.h>
#include <type_traits>

constexpr size_t QUEUE_DEPTH = 2;
constexpr uint32_t REQUEST_INTERVAL_US = 1000;
constexpr uint32_t CONSUMER_PAUSE_MS = 0; // Use 250 for a deliberate slow-consumer test.

enum PacketKind : uint8_t { Setup, Capture, Terminal };
struct CapturePacket {
  hvac_current::RawSequence raw;
  uint64_t request = 0, droppedBefore = 0, startedUs = 0, returnedUs = 0;
  int status = 0;
  PacketKind kind = Setup;
  bool clockOk = false;
};
static_assert(std::is_trivially_copyable<CapturePacket>::value, "Queue copies whole captures");

struct k_msgq captures;
char queueStorage[QUEUE_DEPTH * sizeof(CapturePacket)];
struct k_thread workerThread;
K_THREAD_STACK_DEFINE(workerStack, 4096);

void captureWorker(void*, void*, void*) {
  hvac_current::UnoQSequenceReader reader;
  hvac_current::UnoQUptimeClock clock;
  CapturePacket packet;
  packet.status = reader.initialize();
  k_msgq_put(&captures, &packet, K_FOREVER);
  if (packet.status != 0) return;

  uint64_t dropped = 0;
  for (uint64_t request = 0;; ++request) {
    packet.kind = Capture;
    packet.request = request;
    packet.droppedBefore = dropped;
    packet.clockOk = clock.read(packet.startedUs);
    packet.status = packet.clockOk ? reader.read(packet.raw, REQUEST_INTERVAL_US) : -EIO;
    packet.clockOk = clock.read(packet.returnedUs) && packet.clockOk;
    if (k_msgq_put(&captures, &packet, K_NO_WAIT) != 0 && dropped != UINT64_MAX) ++dropped;

    // A terminal record waits for a queue slot after ADC activity has stopped.
    // Capture puts never wait or overwrite an unread record.
    if (!packet.clockOk || !packet.raw.guardsOk() || request == UINT64_MAX) {
      packet.kind = Terminal;
      packet.status = !packet.clockOk ? 1 : (!packet.raw.guardsOk() ? 2 : 3);
      packet.droppedBefore = dropped;
      k_msgq_put(&captures, &packet, K_FOREVER);
      return;
    }
    k_msleep(100); // No catch-up. This is a requested delay between complete calls.
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
  k_msgq_init(&captures, queueStorage, sizeof(CapturePacket), QUEUE_DEPTH);
  Serial.println("{\"type\":\"hvac_adc_worker\",\"version\":1,\"resolution_bits\":14,\"adc_channels\":[9,10,11],\"sweeps\":16,\"requested_interval_us\":1000,\"queue_depth\":2}");
  k_thread_create(&workerThread, workerStack, K_THREAD_STACK_SIZEOF(workerStack),
                  captureWorker, nullptr, nullptr, nullptr, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
}

void loop() {
  CapturePacket packet;
  if (k_msgq_get(&captures, &packet, K_NO_WAIT) != 0) {
    delay(1);
    return;
  }
  if (packet.kind == Setup) {
    Serial.print("{\"setup_status\":"); Serial.print(packet.status); Serial.println("}");
    return;
  }
  if (packet.kind == Terminal) {
    Serial.print("{\"terminal_reason\":"); Serial.print(packet.status);
    Serial.print(",\"dropped_total\":"); printU64(packet.droppedBefore); Serial.println("}");
    return;
  }

  Serial.print("{\"request\":"); printU64(packet.request);
  Serial.print(",\"dropped_before\":"); printU64(packet.droppedBefore);
  Serial.print(",\"clock_ok\":"); Serial.print(packet.clockOk ? "true" : "false");
  Serial.print(",\"started_us\":"); printU64(packet.startedUs);
  Serial.print(",\"returned_us\":"); printU64(packet.returnedUs);
  Serial.print(",\"driver_status\":"); Serial.print(packet.status);
  Serial.print(",\"guard_ok\":"); Serial.print(packet.raw.guardsOk() ? "true" : "false");
  Serial.print(",\"channels\":[");
  hvac_current::RawFeatures channels[3];
  hvac_current::sequenceFeatures(packet.raw, packet.status, channels);
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
}
