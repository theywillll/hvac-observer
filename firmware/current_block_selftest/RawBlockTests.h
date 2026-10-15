#pragma once
#include <RawBlock.h>

namespace hvac_current_test {
using namespace hvac_current;
using Pool = RawBlockPool<2>;
inline TimingLimits limits() { return {90, 110, 20, 4095, 8}; }
inline bool fill(Pool& p, uint32_t base = 1000, uint32_t c1Extra = 0) {
  for (uint8_t n = 0; n < 2; ++n)
    for (uint8_t c = 0; c < 3; ++c)
      if (!p.append(c, 2048 + c, base + n * 100 + c * 10 +
                    (n && c == 1 ? c1Extra : 0), 5 + c)) return false;
  return p.finish();
}
// Returns the failing source line (zero = pass). Same tests on host and MCU.
inline int run() {
#define CHECK(x) do { if (!(x)) return __LINE__; } while (false)
  Pool p;
  Pool::Lease a, b;
  CHECK(!p.begin()); CHECK(!p.append(0, 0, 0, 1)); CHECK(!p.finish());
  CHECK(!p.acquire(a)); CHECK(!p.view(a)); CHECK(!p.release(a));
  CHECK(!p.reset(0, limits())); CHECK(!p.begin());
  TimingLimits bad = limits(); bad.minIntervalUs = 0;
  CHECK(!p.reset(1, bad)); bad = limits(); bad.maxIntervalUs = 0x80000000u;
  CHECK(!p.reset(1, bad)); bad = limits(); bad.railMargin = 2048;
  CHECK(!p.reset(1, bad)); bad = limits(); bad.maxReadUs = 0;
  CHECK(!p.reset(1, bad)); bad = limits(); bad.maxIntervalUs = 89;
  CHECK(!p.reset(1, bad));
  CHECK(p.reset(1, limits())); CHECK(p.begin()); CHECK(!p.begin()); CHECK(fill(p));
  CHECK(p.acquire(a)); const auto* first = p.view(a);
  CHECK(first && first->usable() && first->session == 1);
  CHECK(first->channels[1].firstUs == 1010 && first->channels[2].lastUs == 1120);
  CHECK(first->channels[0].minIntervalUs == 100 && first->channels[2].maxReadUs == 7);
  CHECK(first->codes[2][1] == 2050);
  CHECK(first->maxSweepSkewUs == 20);
  CHECK(p.begin()); CHECK(fill(p, 2000)); CHECK(!p.begin()); CHECK(p.overruns() == 1);
  CHECK(p.view(a)->codes[2][1] == 2050); // Full queue did not overwrite leased data.
  CHECK(p.acquire(b)); CHECK(b.ticket > a.ticket); CHECK(!p.begin());
  CHECK(p.release(a)); CHECK(!p.release(a)); CHECK(!p.view(a));
  CHECK(p.begin()); CHECK(fill(p, 3000)); CHECK(!p.view(a));
  Pool::Lease next; CHECK(p.acquire(next)); CHECK(next.ticket > b.ticket);
  CHECK(p.release(b)); CHECK(p.release(next));
  CHECK(p.begin()); CHECK(fill(p, UINT32_MAX - 50)); CHECK(p.acquire(a));
  CHECK(p.view(a)->usable()); CHECK(p.view(a)->channels[0].maxIntervalUs == 100);
  CHECK(p.release(a));
  CHECK(p.begin()); CHECK(fill(p, 1000, 11)); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == Timing);
  CHECK(p.view(a)->channels[1].maxIntervalUs == 111);
  CHECK(p.view(a)->channels[0].maxIntervalUs == 100); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(p.append(0, -5, 1000, 21));
  CHECK(p.append(1, 4096, 1010, 1)); CHECK(p.append(2, 8, 1020, 1));
  CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == (Incomplete | DriverError | InvalidCode | NearRail | Timing));
  CHECK(p.view(a)->firstDriverError == -5 && p.view(a)->channels[2].nearRailCount == 1);
  CHECK(!p.view(a)->usable()); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(!p.append(3, 2048, 1000, 1));
  CHECK(!p.append(1, 2048, 1010, 1)); CHECK(fill(p)); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == Order); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(fill(p)); CHECK(p.acquire(a));
  CHECK(p.begin()); CHECK(p.append(0, 2048, 0, 1));
  CHECK(p.reset(2, limits())); CHECK(!p.view(a)); CHECK(!p.release(a)); CHECK(!p.finish());
  CHECK(!p.acquire(b)); CHECK(p.overruns() == 0);
  CHECK(p.begin()); CHECK(p.finish(true)); CHECK(p.acquire(b));
  CHECK(p.view(b)->session == 2 && p.view(b)->faults == (Incomplete | Aborted));
  CHECK(!p.view(a) && !p.release(a)); // Old slot has been reused with a new ticket.
  CHECK(b.ticket > a.ticket); CHECK(p.release(b));
  CHECK(p.begin()); CHECK(fill(p)); CHECK(!p.append(0, 2048, 1200, 1));
  CHECK(p.acquire(a)); CHECK(p.view(a)->usable()); CHECK(p.release(a));
  CHECK(p.begin());
  for (uint8_t n = 0; n < 2; ++n)
    for (uint8_t c = 0; c < 3; ++c) CHECK(p.append(c, 2048, n * 100 + c * 10, 5));
  CHECK(!p.append(0, 2048, 200, 5)); CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == Order); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(p.append(0, 2048, 1000, 10));
  CHECK(p.append(1, 2048, 1005, 1)); CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == (Incomplete | Timing)); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(p.append(0, 4087, 1000, 1));
  CHECK(p.append(1, 4086, 1010, 1)); CHECK(p.append(2, 9, 1020, 1));
  CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == (Incomplete | NearRail));
  CHECK(p.view(a)->channels[0].nearRailCount == 1);
  CHECK(p.view(a)->channels[1].nearRailCount == 0 && p.view(a)->channels[2].nearRailCount == 0);
  CHECK(p.release(a));
  CHECK(p.begin()); CHECK(p.append(0, 2048, 1000, 1));
  CHECK(p.append(1, 2048, 1010, 1)); CHECK(p.append(2, 2048, 1020, 1));
  CHECK(p.append(0, 2048, 1000, 1)); // Repeated per-channel timestamp.
  CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == (Incomplete | Timing)); CHECK(p.release(a));
  CHECK(p.begin()); CHECK(p.append(0, 2048, 1000, 1));
  CHECK(p.append(1, 2048, 999, 1)); CHECK(p.finish()); CHECK(p.acquire(a));
  CHECK(p.view(a)->faults == (Incomplete | Timing)); CHECK(p.release(a));
  // Exercise every slot repeatedly, retaining FIFO order across slot reuse.
  for (uint32_t i = 0; i < 1000; ++i) {
    CHECK(p.begin()); CHECK(fill(p, i * 1000));
    CHECK(p.begin()); CHECK(fill(p, i * 1000 + 500));
    CHECK(!p.begin()); CHECK(p.acquire(a)); CHECK(p.acquire(b));
    CHECK(a.ticket < b.ticket && p.view(a)->usable() && p.view(b)->usable());
    CHECK(p.release(a)); CHECK(p.release(b));
  }
  return 0;
#undef CHECK
}
} // namespace hvac_current_test
