#pragma once
#include <CurrentWire.h>
#include "CurrentWindowTests.h"
#include <string.h>

namespace hvac_wire_test {
using namespace hvac_current;
using hvac_window_test::features;
using hvac_window_test::policy;
using hvac_window_test::stamp;
using hvac_window_test::calibration;
inline bool maximumMetadata(CurrentSnapshotStore& store) {
  auto p=policy(); p.maxAgeUs=0x7fffffffu-15150;
  for(unsigned c=0;c<3;++c) { p.calibrationId[c]=UINT32_MAX-c; p.calibrationRevision[c]=UINT32_MAX; }
  if(!store.reset(UINT64_MAX,p,0)) return false;
  for(unsigned c=0;c<3;++c) {
    auto s=stamp(uint8_t(c),UINT64_MAX); s.session=UINT64_MAX; s.timingEvidenceId=UINT32_MAX;
    auto cal=calibration(uint8_t(c)); cal.recordId=p.calibrationId[c]; cal.revision=UINT32_MAX;
    if(!store.publish(features(),s,cal,16000,uint64_t(16000)+p.maxAgeUs)) return false;
  }
  return true;
}
inline int run() {
#define WIRE_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  CurrentSnapshotStore store;
  char buffer[CurrentWireCapacity];
  WIRE_CHECK(writeCurrentWire(store,0,buffer,sizeof(buffer)));
  WIRE_CHECK(strstr(buffer,"\"compressor_a\":null") != nullptr);
  WIRE_CHECK(strstr(buffer,"\"session\":null") != nullptr);
  WIRE_CHECK(strstr(buffer,"\"window_seq\":null") != nullptr);
  WIRE_CHECK(store.reset(UINT64_MAX,policy(),0));
  for(unsigned c=0;c<3;++c) {
    auto s=stamp(uint8_t(c)); s.session=UINT64_MAX;
    auto cal=calibration(uint8_t(c)); cal.ampsPerCode*=c+1;
    WIRE_CHECK(store.publish(features(),s,cal,16000,17000));
  }
  WIRE_CHECK(writeCurrentWire(store,17000,buffer,sizeof(buffer)));
  WIRE_CHECK(strstr(buffer,"\"compressor_a\":1,\"blower_a\":2,\"condenser_fan_a\":3") != nullptr);
  WIRE_CHECK(strstr(buffer,"\"session\":\"18446744073709551615\"") != nullptr);
  WIRE_CHECK(strstr(buffer,"\"window_seq\":\"0\"") != nullptr);
  const size_t length=strlen(buffer);
  WIRE_CHECK(length+1 < CurrentWireCapacity);
  // Every undersized capacity must clear the payload and preserve its canary.
  for(size_t capacity=0;capacity<=length;++capacity) {
    memset(buffer,'#',sizeof(buffer));
    WIRE_CHECK(!writeCurrentWire(store,17000,buffer,capacity));
    if(capacity) WIRE_CHECK(buffer[0] == '\0');
    for(size_t i=capacity;i<sizeof(buffer);++i) WIRE_CHECK(buffer[i] == '#');
  }
  WIRE_CHECK(writeCurrentWire(store,17000,buffer,length+1));
  WIRE_CHECK(strlen(buffer) == length);
  WIRE_CHECK(!writeCurrentWire(store,17000,nullptr,CurrentWireCapacity));
  WIRE_CHECK(writeCurrentWire(store,21001,buffer,sizeof(buffer)));
  WIRE_CHECK(strstr(buffer,"\"compressor_a\":null") != nullptr);
  WIRE_CHECK(strstr(buffer,"\"faults\":128") != nullptr);
  auto s=stamp(); s.session=UINT64_MAX; s.sequence=UINT64_MAX;
  WIRE_CHECK(store.publish(features(),s,calibration(),16000,22000));
  WIRE_CHECK(writeCurrentWire(store,22000,buffer,sizeof(buffer)));
  WIRE_CHECK(strstr(buffer,"\"window_seq\":\"18446744073709551615\"") != nullptr);
  CurrentSnapshotStore range; WIRE_CHECK(range.reset(1,policy(),0));
  auto cal=calibration(); cal.ampsPerCode=2.01;
  WIRE_CHECK(range.publish(features(),stamp(),cal,16000,17000));
  WIRE_CHECK(!writeCurrentWire(range,17000,buffer,sizeof(buffer)));
  WIRE_CHECK(buffer[0] == '\0'); // 201 A exceeds the receiver's compressor range.
  CurrentSnapshotStore decimal; WIRE_CHECK(decimal.reset(1,policy(),0));
  cal=calibration(); cal.ampsPerCode=0.001;
  WIRE_CHECK(decimal.publish(features(),stamp(),cal,16000,17000));
  WIRE_CHECK(writeCurrentWire(decimal,17000,buffer,sizeof(buffer)));
  WIRE_CHECK(strstr(buffer,"\"compressor_a\":0.1") != nullptr);
  CurrentSnapshotStore maximum; WIRE_CHECK(maximumMetadata(maximum));
  WIRE_CHECK(writeCurrentWire(maximum,uint64_t(16000)+0x7fffffffu-15150,buffer,sizeof(buffer)));
  // Three 32-byte number buffers in place of null still fit this capacity.
  WIRE_CHECK(strlen(buffer)+3*32+1 < CurrentWireCapacity);
  WIRE_CHECK(strstr(buffer,"\"calibration_revision\":4294967295") != nullptr);
  return 0;
#undef WIRE_CHECK
}
} // namespace hvac_wire_test
