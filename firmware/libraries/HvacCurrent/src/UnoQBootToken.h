#pragma once
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "UNO Q boot token adapter requires arduino:zephyr:unoq"
#endif
#include <zephyr/random/random.h>
#include <CurrentBootToken.h>

#if !defined(CONFIG_HARDWARE_DEVICE_CS_GENERATOR) || !defined(CONFIG_ENTROPY_STM32_RNG)
#error "Review the UNO Q boot token entropy source for this core"
#endif

namespace hvac_current {
inline bool initializeUnoQBootToken(CurrentBootToken& token) {
  return token.initialize(sys_csrand_get);
}
} // namespace hvac_current
