#pragma once
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "UNO Q uptime adapter requires arduino:zephyr:unoq"
#endif
#include <zephyr/kernel.h>
#include <CurrentUptime.h>

namespace hvac_current {
static_assert(CONFIG_SYS_CLOCK_TICKS_PER_SEC == CurrentUptime::ticksPerSecond,
              "Review the uptime conversion for this kernel tick rate");

// This adapter deliberately does not consume micros() or infer wrap counts.
// Only the acquisition owner may read it; do not share across ISR/RPC threads.
class UnoQUptimeClock {
 public:
  bool read(uint64_t& nominalUs) { return uptime_.observe(k_uptime_ticks(), nominalUs); }
  UptimeFault fault() const { return uptime_.fault(); }

 private:
  CurrentUptime uptime_;
};
} // namespace hvac_current
