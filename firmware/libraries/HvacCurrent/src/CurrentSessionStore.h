#pragma once
#include <CurrentWire.h>

namespace hvac_current {
// One externally serialized owner for one MCU boot. This is the state contract,
// not an RPC transport or proof of boot identity. Do not expose the inner store.
class CurrentSessionStore {
 public:
  CurrentSessionStore() = default;
  CurrentSessionStore(const CurrentSessionStore&) = delete;
  CurrentSessionStore& operator=(const CurrentSessionStore&) = delete;

  // Every offer revokes publication first, including rejected/repeated offers.
  // A positive, in-range newer ID is consumed even if its policy is rejected.
  bool offer(uint64_t session, WindowPolicy policy, uint64_t nowUs) {
    stop();
    if (!advance(nowUs) || !session || session > maxSession || session <= highest_) return false;
    highest_ = session;
    if (!store_.reset(session, policy, nowUs)) return false;
    pending_ = true;
    return true;
  }

  // Host confirmation must echo exactly the offered ID after receipt of its
  // acceptance reply. Repeated confirmations are harmless; old ones do nothing.
  bool confirm(uint64_t session, uint64_t nowUs) {
    if (!advance(nowUs) || session != highest_ || (!pending_ && !active_)) return false;
    if (pending_) confirmedUs_ = nowUs;
    pending_ = false; active_ = true;
    return true;
  }
  void stop() { pending_ = false; active_ = false; store_.stop(); }
  // A failed clock read cannot be cured by a new session within this owner.
  void clockFailed() { clockFault_ = true; stop(); }
  bool clockFault() const { return clockFault_; }
  bool active() const { return active_; }

  bool publish(const RawFeatures& features, const WindowStamp& stamp,
               const CurrentCalibration& calibration, uint64_t endUs, uint64_t nowUs) {
    if (!advance(nowUs) || !active_) return false;
    // Reject queued windows that started before this confirmation. The existing
    // gate separately checks low-bit consistency, duration and timing evidence.
    const uint64_t span = uint32_t(stamp.lastUs - stamp.firstUs);
    if (endUs < confirmedUs_ || span > endUs - confirmedUs_) return false;
    return store_.publish(features, stamp, calibration, endUs, nowUs);
  }
  // Before confirmation/following stop, omit the optional current payload.
  // Clear a supplied output on failure so callers cannot reuse old bytes.
  bool write(uint64_t nowUs, char* output, size_t capacity) {
    if (output && capacity) output[0] = '\0';
    if (!advance(nowUs) || !active_) return false;
    return writeCurrentWire(store_, nowUs, output, capacity);
  }

 private:
  bool advance(uint64_t nowUs) {
    if (clockFault_) return false;
    if (seenClock_ && nowUs < lastUs_) { clockFailed(); return false; }
    seenClock_ = true; lastUs_ = nowUs;
    return true;
  }
  static constexpr uint64_t maxSession = UINT64_C(9223372036854775807);
  CurrentSnapshotStore store_;
  uint64_t highest_ = 0, lastUs_ = 0, confirmedUs_ = 0;
  bool pending_ = false, active_ = false, seenClock_ = false, clockFault_ = false;
};
} // namespace hvac_current
