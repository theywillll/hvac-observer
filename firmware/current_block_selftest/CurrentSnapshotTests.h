#pragma once
#include <CurrentSnapshot.h>
#include "CurrentWindowTests.h"
#include <type_traits>

namespace hvac_snapshot_test {
using namespace hvac_current;
using hvac_window_test::features;
using hvac_window_test::policy;
using hvac_window_test::stamp;
using hvac_window_test::calibration;
static_assert(!std::is_copy_constructible<CurrentSnapshotStore>::value, "Single owner");
static_assert(!std::is_copy_assignable<CurrentSnapshotStore>::value, "Single owner");
inline int run() {
#define SNAP_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  CurrentSnapshotStore store;
  auto snap=store.snapshot(0);
  for(unsigned c=0;c<3;++c) {
    SNAP_CHECK(snap.channels[c].faults == WindowConfig);
    SNAP_CHECK(isnan(snap.channels[c].rmsAmps));
    SNAP_CHECK(snap.channels[c].channel == c);
  }
  SNAP_CHECK(!store.publish(features(),stamp(),calibration(),16000,17000));
  SNAP_CHECK(store.reset(1,policy(),0));
  snap=store.snapshot(1000);
  SNAP_CHECK(snap.session == 1 && snap.maxAgeUs == 5000);
  for(unsigned c=0;c<3;++c) SNAP_CHECK(snap.channels[c].faults == WindowPending);
  for(unsigned c=0;c<3;++c) {
    auto cal=calibration(uint8_t(c)); cal.ampsPerCode*=c+1;
    SNAP_CHECK(store.publish(features(),stamp(uint8_t(c)),cal,16000,17000));
  }
  snap=store.snapshot(21000); // Inclusive freshness boundary at request time.
  for(unsigned c=0;c<3;++c) {
    SNAP_CHECK(snap.channels[c].available() && snap.channels[c].rmsAmps == c+1);
    SNAP_CHECK(snap.channels[c].ageUs == 5000);
  }
  snap=store.snapshot(21001);
  for(unsigned c=0;c<3;++c) {
    SNAP_CHECK(snap.channels[c].faults == WindowAge);
    SNAP_CHECK(isnan(snap.channels[c].rmsAmps));
  }
  snap=store.snapshot(uint64_t(1)<<32); // No wrapped-age resurrection.
  SNAP_CHECK(snap.channels[0].faults & WindowAge);

  // Correct low 32 bits cannot hide an entire stale timer cycle at publication.
  CurrentSnapshotStore skipped; SNAP_CHECK(skipped.reset(1,policy(),0));
  SNAP_CHECK(skipped.publish(features(),stamp(),calibration(),16000,(uint64_t(1)<<32)+17000));
  snap=skipped.snapshot((uint64_t(1)<<32)+17000);
  SNAP_CHECK(snap.channels[0].faults == WindowAge && isnan(snap.channels[0].rmsAmps));
  SNAP_CHECK(snap.channels[0].ageUs == UINT32_MAX);

  // Ordinary low-counter rollover works when its extended epoch is supplied.
  CurrentSnapshotStore wrap; const uint64_t epoch=uint64_t(1)<<32;
  SNAP_CHECK(wrap.reset(1,policy(),epoch-16000));
  auto ws=stamp(); ws.firstUs=UINT32_MAX-14999; ws.lastUs=0;
  SNAP_CHECK(wrap.publish(features(),ws,calibration(),epoch,epoch+1000));
  SNAP_CHECK(wrap.snapshot(epoch+1000).channels[0].available());
  auto future=stamp(1); future.firstUs+=10000; future.lastUs+=10000;
  SNAP_CHECK(wrap.publish(features(),future,calibration(1),epoch+26000,epoch+2000));
  SNAP_CHECK(wrap.snapshot(epoch+2000).channels[1].faults & WindowAge);
  SNAP_CHECK(wrap.snapshot(epoch+26000).channels[1].faults & WindowAge);

  CurrentSnapshotStore recovery; SNAP_CHECK(recovery.reset(1,policy(),0));
  SNAP_CHECK(recovery.publish(features(),stamp(),calibration(),16000,17000));
  auto bad=features(); bad.faults=FeatureRail;
  SNAP_CHECK(recovery.publish(bad,stamp(0,1),calibration(),16000,17000));
  snap=recovery.snapshot(17000);
  SNAP_CHECK(snap.channels[0].faults & WindowFeatures);
  SNAP_CHECK(isnan(snap.channels[0].rmsAmps));
  SNAP_CHECK(snap.channels[1].faults == WindowPending);
  SNAP_CHECK(recovery.publish(features(),stamp(0,2),calibration(),16000,17000));
  SNAP_CHECK(recovery.snapshot(17000).channels[0].available());
  SNAP_CHECK(recovery.publish(features(),stamp(0,2),calibration(),16000,17000));
  SNAP_CHECK(recovery.snapshot(17000).channels[0].faults == WindowSequence);
  SNAP_CHECK(recovery.publish(features(),stamp(0,3),calibration(),16000,17000));
  // Mismatched extended timestamps revoke the value, even with a valid gate.
  SNAP_CHECK(recovery.publish(features(),stamp(0,4),calibration(),16001,17000));
  SNAP_CHECK(recovery.snapshot(17000).channels[0].faults == WindowTiming);
  SNAP_CHECK(recovery.publish(features(),stamp(0,5),calibration(),16000,17000));
  auto wrong=stamp(); wrong.session=99;
  SNAP_CHECK(!recovery.publish(features(),wrong,calibration(),16000,0));
  wrong=stamp(); wrong.channel=3;
  SNAP_CHECK(!recovery.publish(features(),wrong,calibration(),16000,0));
  SNAP_CHECK(recovery.snapshot(17000).channels[0].available());
  recovery.stop(); snap=recovery.snapshot(18000);
  SNAP_CHECK(snap.channels[0].faults & WindowStopped);
  SNAP_CHECK(isnan(snap.channels[0].rmsAmps));
  SNAP_CHECK(!recovery.publish(features(),stamp(0,6),calibration(),16000,18000));
  SNAP_CHECK(!recovery.reset(1,policy(),0));
  SNAP_CHECK(recovery.snapshot(0).channels[0].faults == WindowConfig);
  SNAP_CHECK(recovery.reset(2,policy(),0));
  SNAP_CHECK(recovery.snapshot(0).channels[0].faults == WindowPending);
  auto fresh=stamp(); fresh.session=2;
  SNAP_CHECK(recovery.publish(features(),fresh,calibration(),16000,17000));
  // A regressing monotonic clock revokes every channel until a new session.
  snap=recovery.snapshot(16999);
  for(unsigned c=0;c<3;++c) SNAP_CHECK(snap.channels[c].faults & WindowClock);
  SNAP_CHECK(!recovery.publish(features(),fresh,calibration(),16000,18000));
  SNAP_CHECK(recovery.reset(3,policy(),0));
  fresh.session=3;
  SNAP_CHECK(recovery.publish(features(),fresh,calibration(),16000,17000));
  fresh.sequence=1;
  SNAP_CHECK(!recovery.publish(features(),fresh,calibration(),16000,16000));
  SNAP_CHECK(recovery.snapshot(18000).channels[0].faults & WindowClock);

  // A fresh, contiguous publication alone can recover an expired channel.
  CurrentSnapshotStore aging; SNAP_CHECK(aging.reset(1,policy(),0));
  SNAP_CHECK(aging.publish(features(),stamp(),calibration(),16000,17000));
  SNAP_CHECK(!aging.snapshot(22000).channels[0].available());
  auto newer=stamp(0,1); newer.firstUs=16000; newer.lastUs=31000;
  SNAP_CHECK(aging.publish(features(),newer,calibration(),31000,32000));
  SNAP_CHECK(aging.snapshot(32000).channels[0].available());
  auto invalid=policy(); invalid.samples=1;
  SNAP_CHECK(!aging.reset(2,invalid,32000));
  SNAP_CHECK(aging.snapshot(32000).channels[0].faults == WindowConfig);
  // Channels with different sample ends expire independently.
  CurrentSnapshotStore independent; SNAP_CHECK(independent.reset(1,policy(),0));
  SNAP_CHECK(independent.publish(features(),stamp(),calibration(),16000,17000));
  auto later=stamp(1); later.firstUs=5000; later.lastUs=20000;
  SNAP_CHECK(independent.publish(features(),later,calibration(1),20000,21000));
  snap=independent.snapshot(22000);
  SNAP_CHECK(snap.channels[0].faults == WindowAge);
  SNAP_CHECK(snap.channels[1].available() && snap.channels[1].ageUs == 2000);
  SNAP_CHECK(snap.channels[1].calibrationId == 10 && snap.channels[1].timingEvidenceId == 7);
  SNAP_CHECK(snap.channels[2].faults == WindowPending);
  // Full-width subtraction is safe at the end of the extended clock range.
  CurrentSnapshotStore limit; SNAP_CHECK(limit.reset(1,policy(),UINT64_MAX-1000));
  auto ls=stamp(); ls.lastUs=uint32_t(UINT64_MAX-1000); ls.firstUs=ls.lastUs-15000;
  SNAP_CHECK(limit.publish(features(),ls,calibration(),UINT64_MAX-1000,UINT64_MAX));
  SNAP_CHECK(limit.snapshot(UINT64_MAX).channels[0].ageUs == 1000);
  SNAP_CHECK(limit.snapshot(UINT64_MAX).channels[0].available());
  SNAP_CHECK(limit.snapshot(0).channels[0].faults == WindowClock);
  return 0;
#undef SNAP_CHECK
}
} // namespace hvac_snapshot_test
