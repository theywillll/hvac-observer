#pragma once
#include "../firmware/current_block_selftest/CurrentWireTests.h"

// Explicitly synthetic transport fixtures; never sensor/board measurements.
inline bool emitCurrentFixture(const char* name, hvac_current::CurrentSnapshotStore& store,
                               uint64_t now) {
  char buffer[hvac_current::CurrentWireCapacity];
  if(!hvac_current::writeCurrentWire(store,now,buffer,sizeof(buffer))) return false;
  printf("{\"fixture\":\"%s\",\"payload\":%s}\n",name,buffer); return true;
}
inline int emitCurrentFixtures() {
  using namespace hvac_wire_test;
  CurrentSnapshotStore store;
  if(!emitCurrentFixture("default",store,0)) return 1;
  if(!store.reset(UINT64_MAX,policy(),0)) return 1;
  if(!emitCurrentFixture("pending",store,0)) return 1;
  for(unsigned c=0;c<3;++c) {
    auto s=stamp(uint8_t(c)); s.session=UINT64_MAX;
    auto cal=calibration(uint8_t(c)); cal.ampsPerCode*=c+1;
    if(!store.publish(features(),s,cal,16000,17000)) return 1;
  }
  if(!emitCurrentFixture("valid",store,17000)) return 1;
  if(!emitCurrentFixture("boundary",store,21000)) return 1;
  if(!emitCurrentFixture("expired",store,21001)) return 1;
  if(!emitCurrentFixture("full_cycle",store,(uint64_t(1)<<32)+17000)) return 1;
  store.stop();
  if(!emitCurrentFixture("stopped",store,(uint64_t(1)<<32)+17000)) return 1;
  CurrentSnapshotStore failed; if(!failed.reset(1,policy(),0)) return 1;
  if(!failed.publish(features(),stamp(),calibration(),16000,17000)) return 1;
  auto bad=features(); bad.faults=FeatureRail;
  if(!failed.publish(bad,stamp(0,1),calibration(),16000,17000)) return 1;
  if(!emitCurrentFixture("failed",failed,17000)) return 1;
  if(!emitCurrentFixture("clock",failed,0)) return 1;
  if(!failed.reset(2,policy(),0)) return 1;
  if(!emitCurrentFixture("reset",failed,0)) return 1;
  CurrentSnapshotStore future; if(!future.reset(1,policy(),0)) return 1;
  if(!future.publish(features(),stamp(),calibration(),16000,15000)) return 1;
  if(!emitCurrentFixture("future",future,15000)) return 1;
  CurrentSnapshotStore maximum; if(!maximumMetadata(maximum)) return 1;
  if(!emitCurrentFixture("maximum",maximum,uint64_t(16000)+0x7fffffffu-15150)) return 1;
  CurrentSnapshotStore decimal; if(!decimal.reset(1,policy(),0)) return 1;
  auto cal=calibration(); cal.ampsPerCode=0.001;
  if(!decimal.publish(features(),stamp(),cal,16000,17000)) return 1;
  if(!emitCurrentFixture("decimal",decimal,17000)) return 1;
  CurrentSnapshotStore zero; if(!zero.reset(1,policy(),0)) return 1;
  RawFeatureAccumulator constant; constant.reset({4095,8,16,16});
  for(unsigned i=0;i<16;++i) constant.add(2050);
  cal=calibration(); cal.minRmsCode=0;
  if(!zero.publish(constant.finish(true),stamp(),cal,16000,17000)) return 1;
  if(!emitCurrentFixture("zero",zero,17000)) return 1;
  return 0;
}
