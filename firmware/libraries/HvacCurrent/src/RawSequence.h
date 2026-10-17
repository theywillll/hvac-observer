#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hvac_current {
// Native 14-bit codes from a single 16-sweep request, in ascending ADC-ID order.
// Guards/sentinels detect some bad writes or incomplete fills after return.
// They are diagnostics, not memory protection or proof of driver correctness.
struct RawSequence {
  static constexpr size_t Channels = 3, Sweeps = 16, Count = Channels * Sweeps;
  static constexpr uint16_t CodeMax = 16383, Guard = 0xa55a, Unwritten = 0xffff;
  volatile uint16_t before;
  uint16_t codes[Count];
  volatile uint16_t after;
  RawSequence() { prepare(); }
  void prepare() {
    before = after = Guard;
    for (size_t i = 0; i < Count; ++i) codes[i] = Unwritten;
  }
  bool guardsOk() const { return before == Guard && after == Guard; }
  bool publishable(int driverStatus) const {
    if (driverStatus != 0 || !guardsOk()) return false;
    for (size_t i = 0; i < Count; ++i) if (codes[i] > CodeMax) return false;
    return true;
  }
};
static_assert(offsetof(RawSequence, codes) == sizeof(uint16_t), "Leading guard layout changed");
static_assert(offsetof(RawSequence, after) == sizeof(uint16_t) * (RawSequence::Count + 1),
              "Trailing guard layout changed");
} // namespace hvac_current
