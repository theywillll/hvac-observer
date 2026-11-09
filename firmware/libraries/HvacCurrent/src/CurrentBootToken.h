#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hvac_current {
// Hold one instance for the lifetime of the MCU session store. Initialize
// before serving requests, from the same serialized owner.
class CurrentBootToken {
 public:
  CurrentBootToken() = default;
  CurrentBootToken(const CurrentBootToken&) = delete;
  CurrentBootToken& operator=(const CurrentBootToken&) = delete;

  // The source returns zero only after filling all bytes. Failures latch.
  template <typename Source>
  bool initialize(Source source) {
    if (attempted_) return token_[0] != '\0';
    attempted_ = true;
    uint8_t bytes[16] = {};
    if (source(bytes, sizeof(bytes)) != 0) return false;

    uint8_t combined = 0;
    for (uint8_t byte : bytes) combined |= byte;
    if (!combined) return false;

    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) {
      token_[2 * i] = digits[bytes[i] >> 4];
      token_[2 * i + 1] = digits[bytes[i] & 15];
    }
    return true;
  }

  // Empty before successful initialization. Suitable for command binding.
  const char (&value() const)[33] { return token_; }

 private:
  bool attempted_ = false;
  char token_[33] = {};
};
} // namespace hvac_current
