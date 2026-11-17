#pragma once
#include <CurrentSessionStore.h>
#include <string.h>

namespace hvac_current {
// Application JSON limit, excluding the terminator. Keep in sync with
// backend/uno_transport.py. The environmental object is generated locally
// and contains no current fields.
static constexpr size_t CurrentFrameCapacity = 897;

inline bool writeCurrentFrame(CurrentSessionStore& sessions, uint64_t nowUs,
                              const char* environment, size_t length,
                              char* output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (capacity > CurrentFrameCapacity) capacity = CurrentFrameCapacity;
  if (!environment || length < 3 || length >= capacity ||
      environment[0] != '{' || environment[length - 1] != '}') return false;

  // Serialize in the remaining space, replacing the two adjacent braces
  // with one comma. On failure the output remains an empty C string.
  const size_t prefix = length - 1;
  if (!sessions.write(nowUs, output + prefix, capacity - prefix)) return false;
  memcpy(output, environment, prefix);
  output[prefix] = ',';
  return true;
}
} // namespace hvac_current
