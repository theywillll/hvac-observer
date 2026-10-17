#pragma once
#include <RawFeatures.h>

namespace hvac_feature_test {
using namespace hvac_current;
inline bool close(double a, double b, double tolerance = 1e-9) { return fabs(a-b) <= tolerance; }
inline int run() {
#define FEATURE_CHECK(x) do { if (!(x)) return __LINE__; } while (false)
  RawFeatureAccumulator a;
  FEATURE_CHECK(!a.add(2048)); FEATURE_CHECK(a.finish(true).faults == FeatureBadConfig);
  FEATURE_CHECK(isnan(a.finish(true).acRmsCode));
  const FeatureConfig invalid[] = {{0,0,2,16},{4095,2048,2,16},{4095,8,1,16},
                                   {4095,8,17,16},{4095,8,2,65536}};
  for (const auto& config : invalid) FEATURE_CHECK(!a.reset(config));
  FEATURE_CHECK(a.reset({4095,8,16,800}));
  for (int i=0;i<16;++i) FEATURE_CHECK(a.add(2048));
  auto f = a.finish(); FEATURE_CHECK(f.faults == FeatureSource && isnan(f.meanCode));
  f = a.finish(true); FEATURE_CHECK(f.statisticsAvailable());
  FEATURE_CHECK(f.meanCode == 2048 && f.acRmsCode == 0 && f.sampleCount == 16);
  FEATURE_CHECK(a.reset({4095,8,16,800}));
  for (int i=0;i<800;++i) FEATURE_CHECK(a.add(i%2 ? 2150 : 1950));
  f = a.finish(true); FEATURE_CHECK(f.meanCode == 2050 && f.acRmsCode == 100);
  FEATURE_CHECK(!a.add(2048)); f = a.finish(true);
  FEATURE_CHECK(f.faults == FeatureOverflow && f.attempts == 800 && isnan(f.acRmsCode));
  FEATURE_CHECK(a.reset({4095,8,16,800}));
  for (int i=0;i<15;++i) FEATURE_CHECK(a.add(2048));
  FEATURE_CHECK(a.finish(true).faults == FeatureShort);
  FEATURE_CHECK(a.reset({4095,8,2,16}));
  const int railCodes[] = {8,9,4086,4087};
  for (int code : railCodes) FEATURE_CHECK(a.add(code));
  f = a.finish(true); FEATURE_CHECK(f.faults == FeatureRail && f.nearRailCount == 2);
  FEATURE_CHECK(isnan(f.acRmsCode) && f.minCode == 8 && f.maxCode == 4087);
  FEATURE_CHECK(!a.add(-5)); FEATURE_CHECK(!a.add(4096)); FEATURE_CHECK(!a.add(INT32_MAX));
  f = a.finish(true); FEATURE_CHECK(f.faults == (FeatureRail | FeatureCode));
  FEATURE_CHECK(f.sampleCount == 4 && f.attempts == 7);
  // Exact mean removal must preserve tiny ripple at nearly full-scale DC.
  FEATURE_CHECK(a.reset({65535,0,2,65535}));
  for (uint32_t i=0;i<65535;++i) FEATURE_CHECK(a.add(i%2 ? 65534 : 65533));
  f = a.finish(true); FEATURE_CHECK(f.statisticsAvailable());
  FEATURE_CHECK(close(f.acRmsCode, sqrt(32768.0*32767.0)/65535.0, 1e-12));
  FEATURE_CHECK(close(f.meanCode, (32768.0*65533.0+32767.0*65534.0)/65535.0, 1e-10));
  RawFeatureAccumulator shifted;
  FEATURE_CHECK(a.reset({16383,8,1280,1280}));
  FEATURE_CHECK(shifted.reset({16383,8,1280,1280}));
  for (int i=0;i<1280;++i) {
    const int wave = int(round(2000.0*sin(6.283185307179586*i/128.0)));
    FEATURE_CHECK(a.add(8000+wave)); FEATURE_CHECK(shifted.add(12000+wave));
  }
  f = a.finish(true); const auto offset = shifted.finish(true);
  FEATURE_CHECK(f.statisticsAvailable() && offset.statisticsAvailable());
  FEATURE_CHECK(close(f.meanCode,8000) && close(offset.meanCode,12000));
  FEATURE_CHECK(close(f.acRmsCode,2000/sqrt(2.0),0.5));
  FEATURE_CHECK(close(f.acRmsCode,offset.acRmsCode));
  RawFeatureAccumulator perChannel[3];
  for (int c=0;c<3;++c) {
    FEATURE_CHECK(perChannel[c].reset({4095,8,16,16}));
    for (int i=0;i<16;++i) FEATURE_CHECK(perChannel[c].add(2000+(i%2 ? 1 : -1)*(c+1)*100));
  }
  for (int c=0;c<3;++c) {
    const auto channel = perChannel[c].finish(true);
    FEATURE_CHECK(channel.statisticsAvailable() && channel.meanCode == 2000);
    FEATURE_CHECK(channel.acRmsCode == (c+1)*100);
  }
  // Independent centered, two-pass reference across varied synthetic windows.
  uint32_t seed = 17;
  for (int window=0;window<100;++window) {
    uint16_t values[64]; double mean=0, variance=0;
    FEATURE_CHECK(a.reset({4095,8,64,64}));
    for (int i=0;i<64;++i) {
      seed = seed*1664525u + 1013904223u;
      values[i] = uint16_t(1000 + seed%2001); mean += values[i];
      FEATURE_CHECK(a.add(values[i]));
    }
    mean /= 64;
    for (int i=0;i<64;++i) variance += (values[i]-mean)*(values[i]-mean);
    f = a.finish(true); FEATURE_CHECK(f.statisticsAvailable());
    FEATURE_CHECK(close(f.meanCode,mean) && close(f.acRmsCode,sqrt(variance/64)));
  }
  // Reset drops old bad-window state and never exposes stale feature values.
  FEATURE_CHECK(a.reset({4095,8,2,16})); FEATURE_CHECK(!a.add(-1));
  FEATURE_CHECK(isnan(a.finish(true).acRmsCode));
  FEATURE_CHECK(a.reset({4095,8,2,16}));
  FEATURE_CHECK(a.finish(true).sampleCount == 0 && isnan(a.finish(true).acRmsCode));
  FEATURE_CHECK(a.add(1000) && a.add(1200)); f = a.finish(true);
  FEATURE_CHECK(f.statisticsAvailable() && f.meanCode == 1100 && f.acRmsCode == 100);
  return 0;
#undef FEATURE_CHECK
}
} // namespace hvac_feature_test
