#pragma once
#include <stdint.h>

namespace hvac_current {

enum class UptimeFault : uint8_t { None, Negative, Regression, Overflow };

// Pinned UNO Q kernel tick domain. This is a coarse boot-relative clock,
// not an ADC aperture clock. One owner serializes observations per MCU boot.
class CurrentUptime {
 public:
  static constexpr uint32_t ticksPerSecond = 10000;
  static constexpr uint32_t usPerTick = 100;

  CurrentUptime() = default;
  CurrentUptime(const CurrentUptime&) = delete;
  CurrentUptime& operator=(const CurrentUptime&) = delete;

  // Equal ticks are normal. Never return an earlier reading on failure.
  // A session change cannot clear a clock fault; there is no reset method.
  bool observe(int64_t ticks, uint64_t& nominalUs) {
    nominalUs = 0;
    if (fault_ != UptimeFault::None) return false;
    if (ticks < 0) fault_ = UptimeFault::Negative;
    else if (ticks < lastTicks_) fault_ = UptimeFault::Regression;
    else if (uint64_t(ticks) > UINT64_MAX / usPerTick) fault_ = UptimeFault::Overflow;
    if (fault_ != UptimeFault::None) return false;
    lastTicks_ = ticks;
    nominalUs = uint64_t(ticks) * usPerTick;
    return true;
  }

  UptimeFault fault() const { return fault_; }

 private:
  int64_t lastTicks_ = -1;
  UptimeFault fault_ = UptimeFault::None;
};

} // namespace hvac_current
