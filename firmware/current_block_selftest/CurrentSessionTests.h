#pragma once
#include <CurrentSessionStore.h>
#include <CurrentSessionCommand.h>
#include <CurrentBootToken.h>
#include <CurrentFrame.h>
#include "CurrentWindowTests.h"
#include <string.h>
#include <type_traits>

namespace hvac_session_test {
inline int runCaptureTickets() {
  using namespace hvac_current;
  using hvac_window_test::policy;
#define TICKET_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  CurrentSessionStore store(100);
  CurrentCaptureTicket first, second, old;
  TICKET_CHECK(!store.beginCapture(0, first) && !first.session && !first.ordinal);
  TICKET_CHECK(store.offer(1, policy(), 0));
  TICKET_CHECK(!store.beginCapture(10, first));
  TICKET_CHECK(store.confirm(1, 10) && store.beginCapture(10, first));
  TICKET_CHECK(first.session == 1 && first.ordinal == 1 && first.issuedUs == 10);
  TICKET_CHECK(!store.beginCapture(10, second) && !second.ordinal);
  old = first;
  ++old.ordinal;
  TICKET_CHECK(!store.finishCapture(old, 20));
  old = first;
  ++old.issuedUs;
  TICKET_CHECK(!store.finishCapture(old, 20));
  TICKET_CHECK(store.finishCapture(first, 20));
  TICKET_CHECK(!store.finishCapture(first, 20));
  TICKET_CHECK(store.beginCapture(20, second) && second.ordinal == 2);
  TICKET_CHECK(!store.finishCapture(first, 20));
  TICKET_CHECK(store.finishCapture(second, 20));
  TICKET_CHECK(store.beginCapture(20, second) && second.ordinal == 3);
  store.stop();
  TICKET_CHECK(!store.finishCapture(second, 20));
  TICKET_CHECK(store.offer(2, policy(), 20) && store.confirm(2, 20));
  TICKET_CHECK(store.beginCapture(20, second) && second.ordinal == 1);
  TICKET_CHECK(!store.finishCapture(first, 21));
  TICKET_CHECK(store.finishCapture(second, 21));
  TICKET_CHECK(store.beginCapture(21, second));
  TICKET_CHECK(!store.finishCapture(second, 120)); // Lease expired at confirmation + 100.
  TICKET_CHECK(!store.active());
  TICKET_CHECK(store.offer(3, policy(), 120) && store.confirm(3, 120));
  TICKET_CHECK(store.beginCapture(120, second));
  TICKET_CHECK(!store.finishCapture(second, 119) && store.clockFault());
  TICKET_CHECK(!store.beginCapture(121, first) && !first.session);

  CurrentSessionStore wrap(100);
  const uint64_t beforeWrap = UINT64_C(4294967290);
  TICKET_CHECK(wrap.offer(1, policy(), beforeWrap) && wrap.confirm(1, beforeWrap));
  TICKET_CHECK(wrap.beginCapture(beforeWrap, first));
  TICKET_CHECK(wrap.finishCapture(first, beforeWrap + 20));
  CurrentSessionStore renewed(100);
  TICKET_CHECK(renewed.offer(1, policy(), 0) && renewed.confirm(1, 0));
  TICKET_CHECK(renewed.beginCapture(0, first) && renewed.renew(1, 90));
  TICKET_CHECK(renewed.finishCapture(first, 150));
  CurrentSessionStore changed;
  TICKET_CHECK(changed.offer(1, policy(), 0) && changed.confirm(1, 0));
  TICKET_CHECK(changed.beginCapture(0, first));
  auto bad = policy(); bad.samples = 0;
  TICKET_CHECK(!changed.offer(2, bad, 1));
  TICKET_CHECK(!changed.finishCapture(first, 2));
  CurrentSessionStore unqualified;
  TICKET_CHECK(unqualified.offer(1, policy(), 0) && unqualified.confirm(1, 0));
  TICKET_CHECK(unqualified.beginCapture(0, first));
  RawFeatures channels[3] = {
    hvac_window_test::features(), hvac_window_test::features(), hvac_window_test::features()
  };
  channels[1].faults = FeatureRail;
  TICKET_CHECK(unqualified.finishUnqualifiedCapture(first, channels, 1));
  TICKET_CHECK(!unqualified.finishUnqualifiedCapture(first, channels, 1));
  char output[CurrentWireCapacity];
  TICKET_CHECK(unqualified.write(1, output, sizeof(output)));
  TICKET_CHECK(strstr(output, "\"faults\":2368") && strstr(output, "\"faults\":2400"));
  TICKET_CHECK(strstr(output, "\"compressor_a\":null") && strstr(output, "\"window_seq\":null"));
  TICKET_CHECK(unqualified.offer(2, policy(), 2) && unqualified.confirm(2, 2));
  TICKET_CHECK(!unqualified.finishUnqualifiedCapture(first, channels, 2));
  TICKET_CHECK(unqualified.write(2, output, sizeof(output)));
  TICKET_CHECK(strstr(output, "\"faults\":2048") && !strstr(output, "\"faults\":2400"));
  return 0;
#undef TICKET_CHECK
}

inline int runBootToken() {
  hvac_current::CurrentBootToken valid, failed, zero;
  int calls = 0;
  const auto source = [&](uint8_t* bytes, size_t length) {
    ++calls;
    if (length != 16) return -1;
    for (size_t i = 0; i < length; ++i) bytes[i] = uint8_t(i * 17);
    return 0;
  };
  if (valid.value()[0] || !valid.initialize(source) || !valid.initialize(source) ||
      calls != 1 || strcmp(valid.value(), "00112233445566778899aabbccddeeff")) return __LINE__;
  if (failed.initialize([](uint8_t* bytes, size_t) { bytes[0] = 1; return -1; }) ||
      failed.value()[0] || failed.initialize(source) || calls != 1) return __LINE__;
  if (zero.initialize([](uint8_t*, size_t) { return 0; }) || zero.value()[0] ||
      zero.initialize(source) || calls != 1) return __LINE__;
  return 0;
}

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
  if (!send(second, secondBoot, "1 R 00000000000000000000000000000002 1") ||
      strcmp(reply, "1 L 00000000000000000000000000000002 1")) return __LINE__;
  if (send(second, secondBoot, "1 R 00000000000000000000000000000001 1")) return __LINE__;

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
  const int ticketFailure = runCaptureTickets();
  if (ticketFailure) return ticketFailure;
  const int bootFailure = runBootToken();
  if (bootFailure) return bootFailure;
  const int commandFailure = runCommands();
  if (commandFailure) return commandFailure;
  using namespace hvac_current;
  using namespace hvac_window_test;
  static_assert(!std::is_copy_constructible<CurrentSessionStore>::value, "One boot owner");
  static_assert(!std::is_copy_assignable<CurrentSessionStore>::value, "One boot owner");
#define SESSION_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  CurrentSessionStore leased(100);
  SESSION_CHECK(leased.offer(1,policy(),0));
  SESSION_CHECK(!leased.renew(1,50));
  SESSION_CHECK(!leased.confirm(1,100));
  SESSION_CHECK(!leased.offer(1,policy(),100));
  SESSION_CHECK(leased.offer(2,policy(),100) && leased.confirm(2,190));
  SESSION_CHECK(leased.renew(2,289));
  SESSION_CHECK(!leased.renew(1,388));
  SESSION_CHECK(leased.confirm(2,388)); // Duplicate confirmation is not renewal.
  SESSION_CHECK(!leased.renew(2,389));
  SESSION_CHECK(!leased.active() && !leased.confirm(2,389));
  SESSION_CHECK(leased.offer(3,policy(),389) && leased.confirm(3,389));
  leased.expire(489);
  SESSION_CHECK(!leased.active() && !leased.confirm(3,489));
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
  const size_t payloadLength = strlen(text);
  char frame[CurrentFrameCapacity + 1];
  char environment[CurrentFrameCapacity];
  memset(environment, ' ', sizeof(environment));
  memcpy(environment, "{\"seq\":0", 8);
  const size_t exactLength = CurrentFrameCapacity - payloadLength;
  environment[exactLength - 1] = '}';
  frame[CurrentFrameCapacity] = '#';
  SESSION_CHECK(writeCurrentFrame(s,1000,environment,exactLength,frame,sizeof(frame)));
  SESSION_CHECK(strlen(frame) == CurrentFrameCapacity - 1);
  SESSION_CHECK(frame[exactLength - 1] == ',' && frame[CurrentFrameCapacity] == '#');
  SESSION_CHECK(strstr(frame,"\"session\":\"1\"") != nullptr);
  environment[exactLength - 1] = ' ';
  environment[exactLength] = '}';
  SESSION_CHECK(!writeCurrentFrame(s,1000,environment,exactLength+1,frame,sizeof(frame)));
  SESSION_CHECK(!frame[0] && frame[CurrentFrameCapacity] == '#');
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
  SESSION_CHECK(!writeCurrentFrame(s,37001,"{\"seq\":0}",9,frame,sizeof(frame)) && !frame[0]);
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
