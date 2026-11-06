#pragma once
#include <CurrentSessionStore.h>
#include <string.h>

namespace hvac_current {
// Requests: "1 O <boot> <session>" or "1 C <boot> <session>".
// Replies echo the request with A (accepted) or K (confirmed) in place of O/C.
// Length excludes the terminator. The caller owns the serialized store, an
// immutable token for this boot and the local policy. The token must be fresh
// across boots. This function does not generate tokens or authenticate peers.
static constexpr size_t CurrentSessionCommandCapacity = 57;

inline bool applyCurrentSessionCommand(CurrentSessionStore& store,
                                      const char (&boot)[33],
                                      const char* request, size_t length,
                                      const WindowPolicy& policy, uint64_t nowUs,
                                      char* reply, size_t capacity) {
  const auto reject = [&]() {
    if (reply && capacity) reply[0] = '\0';
    return false;
  };
  if (!request || !reply || length < 38 || length >= CurrentSessionCommandCapacity ||
      capacity <= length || boot[32] != '\0') return reject();
  if (request[0] != '1' || request[1] != ' ' ||
      (request[2] != 'O' && request[2] != 'C') || request[3] != ' ' ||
      request[36] != ' ' || memcmp(request + 4, boot, 32) != 0) return reject();

  bool nonzero = false;
  for (size_t i = 0; i < 32; ++i) {
    const char c = boot[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return reject();
    nonzero = nonzero || c != '0';
  }
  if (!nonzero || request[37] == '0') return reject();

  uint64_t session = 0;
  for (size_t i = 37; i < length; ++i) {
    const char c = request[i];
    if (c < '0' || c > '9') return reject();
    const uint64_t digit = uint64_t(c - '0');
    if (session > (CurrentSessionMax - digit) / 10) return reject();
    session = session * 10 + digit;
  }

  const bool offer = request[2] == 'O';
  const bool accepted = offer ? store.offer(session, policy, nowUs) : store.confirm(session, nowUs);
  if (!accepted) return reject();
  memmove(reply, request, length);
  reply[2] = offer ? 'A' : 'K';
  reply[length] = '\0';
  return true;
}
} // namespace hvac_current
