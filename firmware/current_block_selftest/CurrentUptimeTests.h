#pragma once
#include <CurrentUptime.h>

namespace hvac_uptime_test {
inline int run() {
  using namespace hvac_current;
  CurrentUptime clock;
  uint64_t us = 99;
  if (!clock.observe(0, us) || us != 0) return __LINE__;
  if (!clock.observe(10000, us) || us != 1000000) return __LINE__;
  if (!clock.observe(10000, us) || us != 1000000) return __LINE__;

  // These consecutive 100-us ticks straddle the 32-bit microsecond wrap.
  if (!clock.observe(42949672, us) || us != UINT64_C(4294967200)) return __LINE__;
  if (!clock.observe(42949673, us) || us != UINT64_C(4294967300) ||
      uint32_t(us) != 4) return __LINE__;
  // A reader may skip multiple wraps; the kernel supplies the full tick count.
  if (!clock.observe(171798692, us) || us != UINT64_C(17179869200)) return __LINE__;
  if (clock.observe(1, us) || us != 0 || clock.fault() != UptimeFault::Regression) return __LINE__;
  if (clock.observe(171798693, us) || us != 0) return __LINE__;

  CurrentUptime invalid;
  if (invalid.observe(-1, us) || us != 0 || invalid.fault() != UptimeFault::Negative) return __LINE__;
  if (invalid.observe(0, us)) return __LINE__;
  CurrentUptime limit;
  const int64_t last = int64_t(UINT64_MAX / CurrentUptime::usPerTick);
  if (!limit.observe(last, us) || us != UINT64_C(18446744073709551600)) return __LINE__;
  if (limit.observe(last + 1, us) || us != 0 || limit.fault() != UptimeFault::Overflow) return __LINE__;
  if (limit.observe(last, us)) return __LINE__;
  return 0;
}
}
