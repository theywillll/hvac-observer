#pragma once
#include <CurrentSessionStore.h>
#include <CurrentSessionCommand.h>
#include "CurrentWindowTests.h"
#include <string.h>
#include <type_traits>

namespace hvac_session_test {
inline int runCommands() {
  using namespace hvac_current;
  const char firstBoot[] = "00000000000000000000000000000001";
  const char secondBoot[] = "00000000000000000000000000000002";
  CurrentSessionStore first, second;
  char reply[CurrentSessionCommandCapacity];
  const auto policy = hvac_window_test::policy();
  const auto send = [&](CurrentSessionStore& store, const char (&boot)[33], const char* input) {
    return applyCurrentSessionCommand(store, boot, input, strlen(input), policy, 100, reply, sizeof(reply));
  };
  const char offer[] = "1 O 00000000000000000000000000000001 9223372036854775807";
  const char confirm[] = "1 C 00000000000000000000000000000001 9223372036854775807";
  if (applyCurrentSessionCommand(first, firstBoot, offer, sizeof(offer)-1,
                                policy, 100, reply, sizeof(offer)-1)) return __LINE__;
  if (!send(first, firstBoot, offer) || first.active() ||
      strcmp(reply, "1 A 00000000000000000000000000000001 9223372036854775807")) return __LINE__;
  if (!send(first, firstBoot, confirm) || !first.active() ||
      strcmp(reply, "1 K 00000000000000000000000000000001 9223372036854775807")) return __LINE__;
  if (!send(second, secondBoot, "1 O 00000000000000000000000000000002 1")) return __LINE__;
  if (send(second, secondBoot, offer) || reply[0]) return __LINE__;
  if (send(second, secondBoot, "1 C 00000000000000000000000000000001 1") ||
      reply[0] || second.active()) return __LINE__;
  if (!send(second, secondBoot, "1 C 00000000000000000000000000000002 1") || !second.active()) return __LINE__;

  const char* bad[] = {
    "2 O 00000000000000000000000000000002 2",
    "1 A 00000000000000000000000000000002 2",
    "1 O 00000000000000000000000000000002 0",
    "1 O 00000000000000000000000000000002 02",
    "1 O 00000000000000000000000000000002 +2",
    "1 O 00000000000000000000000000000002 2\n",
    "1 O 00000000000000000000000000000002 9223372036854775808",
    "1 O 00000000000000000000000000000002 18446744073709551616",
  };
  for (const char* input : bad)
    if (send(second, secondBoot, input) || reply[0] || !second.active()) return __LINE__;
  char embedded[] = "1 O 00000000000000000000000000000002 2\0x";
  if (applyCurrentSessionCommand(second, secondBoot, embedded, sizeof(embedded)-1,
                                 policy, 100, reply, sizeof(reply))) return __LINE__;
  if (!second.active()) return __LINE__;
  const char zero[] = "00000000000000000000000000000000";
  if (send(second, zero, "1 O 00000000000000000000000000000000 2")) return __LINE__;
  if (!send(second, secondBoot, "1 O 00000000000000000000000000000002 2") || second.active()) return __LINE__;
  return 0;
}

inline int run() {
  const int commandFailure = runCommands();
  if (commandFailure) return commandFailure;
  using namespace hvac_current;
  using namespace hvac_window_test;
  static_assert(!std::is_copy_constructible<CurrentSessionStore>::value, "One boot owner");
  static_assert(!std::is_copy_assignable<CurrentSessionStore>::value, "One boot owner");
#define SESSION_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  CurrentSessionStore s;
  char text[CurrentWireCapacity] = "old";
  SESSION_CHECK(!s.write(0,text,sizeof(text)) && text[0] == '\0');
  SESSION_CHECK(!s.confirm(0,0));
  SESSION_CHECK(!s.offer(0,policy(),0));
  SESSION_CHECK(!s.offer(UINT64_MAX,policy(),0));
  SESSION_CHECK(s.offer(1,policy(),0));
  SESSION_CHECK(!s.active());
  SESSION_CHECK(!s.publish(features(),stamp(),calibration(),16000,1000));
  SESSION_CHECK(!s.write(1000,text,sizeof(text)));
  SESSION_CHECK(!s.confirm(2,1000));
  SESSION_CHECK(s.confirm(1,1000) && s.active());
  SESSION_CHECK(s.confirm(1,1000));
  SESSION_CHECK(s.write(1000,text,sizeof(text)));
  SESSION_CHECK(strstr(text,"\"compressor_a\":null") != nullptr);
  SESSION_CHECK(s.publish(features(),stamp(),calibration(),16000,17000));
  SESSION_CHECK(s.write(17000,text,sizeof(text)));
  SESSION_CHECK(strstr(text,"\"compressor_a\":1,") != nullptr);
  SESSION_CHECK(s.offer(2,policy(),17000) && !s.active());
  SESSION_CHECK(!s.confirm(1,17000));
  SESSION_CHECK(!s.write(17000,text,sizeof(text)) && !text[0]);
  SESSION_CHECK(s.confirm(2,17000));
  SESSION_CHECK(!s.publish(features(),stamp(),calibration(),16000,17000));
  SESSION_CHECK(s.write(17000,text,sizeof(text)));
  SESSION_CHECK(strstr(text,"\"compressor_a\":null") != nullptr);
  auto fresh = stamp(); fresh.session = 2;
  SESSION_CHECK(!s.publish(features(),fresh,calibration(),16000,17000));
  fresh.firstUs = 16000; fresh.lastUs = 31000;
  SESSION_CHECK(!s.publish(features(),fresh,calibration(),31000,31000));
  fresh.firstUs = 17000; fresh.lastUs = 32000;
  SESSION_CHECK(s.confirm(2,31000)); // duplicate must not move confirmation time
  SESSION_CHECK(s.publish(features(),fresh,calibration(),32000,33000));
  SESSION_CHECK(s.write(37001,text,sizeof(text)));
  SESSION_CHECK(strstr(text,"\"compressor_a\":null") != nullptr); // normal age gate retained
  s.stop();
  SESSION_CHECK(!s.confirm(2,37001));
  SESSION_CHECK(!s.offer(2,policy(),37001));
  auto bad = policy(); bad.samples = 0;
  SESSION_CHECK(!s.offer(3,bad,37001));
  SESSION_CHECK(!s.offer(3,policy(),37001)); // failed configuration consumes ID
  SESSION_CHECK(s.offer(4,policy(),37001));
  SESSION_CHECK(!s.offer(4,policy(),37001)); // duplicate offer revokes pending
  SESSION_CHECK(!s.confirm(4,37001));
  SESSION_CHECK(s.offer(5,policy(),37001));
  SESSION_CHECK(s.confirm(5,37001));
  SESSION_CHECK(!s.write(37000,text,sizeof(text)) && !text[0]);
  SESSION_CHECK(s.clockFault() && !s.active());
  SESSION_CHECK(!s.offer(6,policy(),38000));
  CurrentSessionStore failedRead;
  SESSION_CHECK(failedRead.offer(1,policy(),0));
  failedRead.clockFailed();
  SESSION_CHECK(!failedRead.confirm(1,0));
  SESSION_CHECK(!failedRead.offer(2,policy(),100));
  CurrentSessionStore limit;
  const uint64_t max = UINT64_C(9223372036854775807);
  SESSION_CHECK(limit.offer(max,policy(),0) && limit.confirm(max,0));
  SESSION_CHECK(!limit.offer(max+1,policy(),0) && !limit.active());
  SESSION_CHECK(!limit.offer(1,policy(),0));
  SESSION_CHECK(!limit.write(0,nullptr,0));
  return 0;
#undef SESSION_CHECK
}
}
