#pragma once
#include <UnoQSequenceReader.h>
#include <UnoQUptimeClock.h>
#include <CurrentSessionStore.h>
#include <type_traits>

namespace hvac_current {
enum class CapturePacketKind : uint8_t { Setup, Capture, Terminal };
struct CapturePacket {
  RawSequence raw;
  CurrentCaptureTicket ticket;
  uint64_t request = 0, startedUs = 0, returnedUs = 0;
  int status = 0;
  CapturePacketKind kind = CapturePacketKind::Setup;
  bool clockOk = false;
};
static_assert(std::is_trivially_copyable<CapturePacket>::value, "Queue copies whole captures");

// Global boot-lifetime object. Only the sketch owner calls begin/submit/poll.
// The thread owns ADC and its diagnostic clock, and exchanges copies only.
class UnoQCaptureWorker {
 public:
  UnoQCaptureWorker() = default;
  UnoQCaptureWorker(const UnoQCaptureWorker&) = delete;
  UnoQCaptureWorker& operator=(const UnoQCaptureWorker&) = delete;

  bool begin() {
    if (started_) return false;
    started_ = true;
    k_msgq_init(&captures_, captureStorage_, sizeof(CapturePacket), 2);
    k_msgq_init(&requests_, requestStorage_, sizeof(CurrentCaptureTicket), 1);
    return k_thread_create(&thread_, stack_, K_KERNEL_STACK_SIZEOF(stack_),
                           entry, this, nullptr, nullptr, K_PRIO_PREEMPT(7), 0, K_NO_WAIT) != nullptr;
  }
  bool submit(const CurrentCaptureTicket& ticket) {
    return started_ && k_msgq_put(&requests_, &ticket, K_NO_WAIT) == 0;
  }
  bool poll(CapturePacket& packet) {
    return started_ && k_msgq_get(&captures_, &packet, K_NO_WAIT) == 0;
  }

 private:
  static void entry(void* instance, void*, void*) {
    static_cast<UnoQCaptureWorker*>(instance)->run();
  }
  void run() {
    UnoQSequenceReader reader;
    UnoQUptimeClock clock;
    CapturePacket packet;
    packet.status = reader.initialize();
    k_msgq_put(&captures_, &packet, K_FOREVER);
    if (packet.status != 0) return;
    for (uint64_t request = 0;; ++request) {
      if (k_msgq_get(&requests_, &packet.ticket, K_FOREVER) != 0) return;
      packet.kind = CapturePacketKind::Capture;
      packet.request = request;
      packet.clockOk = clock.read(packet.startedUs);
      packet.status = packet.clockOk ? reader.read(packet.raw, 1000) : -EIO;
      packet.clockOk = clock.read(packet.returnedUs) && packet.clockOk;
      k_msgq_put(&captures_, &packet, K_FOREVER);
      if (!packet.clockOk || !packet.raw.guardsOk() || request == UINT64_MAX) {
        packet.kind = CapturePacketKind::Terminal;
        packet.status = !packet.clockOk ? 1 : (!packet.raw.guardsOk() ? 2 : 3);
        k_msgq_put(&captures_, &packet, K_FOREVER);
        return;
      }
      k_msleep(100);
    }
  }

  k_msgq captures_{}, requests_{};
  char captureStorage_[2 * sizeof(CapturePacket)];
  char requestStorage_[sizeof(CurrentCaptureTicket)];
  k_thread thread_{};
  K_KERNEL_STACK_MEMBER(stack_, 4096);
  bool started_ = false;
};
} // namespace hvac_current
