#pragma once
#include <CurrentSessionStore.h>
#include "CurrentWindowTests.h"
#include <string.h>
#include <type_traits>

namespace hvac_session_test {
inline int run() {
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
