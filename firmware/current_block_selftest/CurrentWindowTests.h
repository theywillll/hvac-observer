#pragma once
#include <CurrentWindow.h>
#include <float.h>

namespace hvac_window_test {
using namespace hvac_current;
// All records below are synthetic fixtures, never real bench qualification.
inline WindowPolicy policy() {
  WindowPolicy p; p.samples=16; p.minIntervalUs=990; p.maxIntervalUs=1010;
  p.maxAgeUs=5000; p.codeMax=4095;
  for(unsigned c=0;c<3;++c) { p.calibrationId[c]=9+c; p.calibrationRevision[c]=2; }
  return p;
}
inline RawFeatures features() {
  RawFeatureAccumulator a; a.reset({4095,8,16,16});
  for (int i=0;i<16;++i) a.add(i%2 ? 2150 : 1950);
  return a.finish(true);
}
inline WindowStamp stamp(uint8_t channel=0, uint64_t sequence=0) {
  WindowStamp s; s.session=1; s.channel=channel; s.sequence=sequence;
  s.kind=SampleTimeKind::QualifiedAperture; s.timingEvidenceId=7;
  s.firstUs=1000; s.lastUs=16000; s.intervalsObserved=15;
  s.minIntervalUs=s.maxIntervalUs=1000; return s;
}
inline CurrentCalibration calibration(uint8_t channel=0) {
  CurrentCalibration c; c.schema=1; c.recordId=9+channel; c.revision=2; c.evidenceId=11;
  c.channel=channel; c.codeMax=4095; c.benchQualified=true;
  c.ampsPerCode=0.01; c.minRmsCode=10; c.maxRmsCode=1000; return c;
}
inline CurrentWindow once(RawFeatures f, WindowStamp s, CurrentCalibration c, uint32_t now=17000) {
  CurrentWindowGate g; g.reset(1,policy()); return g.evaluate(f,s,c,now);
}
inline int run() {
#define WINDOW_CHECK(x) do { if (!(x)) return __LINE__; } while(false)
  const auto f=features(); const auto s=stamp(); const auto c=calibration();
  CurrentWindowGate g;
  WINDOW_CHECK(g.evaluate(f,s,c,17000).faults == WindowConfig);
  WINDOW_CHECK(!g.reset(0,policy()));
  auto p=policy(); p.samples=1; WINDOW_CHECK(!g.reset(1,p));
  p=policy(); p.maxIntervalUs=0x80000000u; WINDOW_CHECK(!g.reset(1,p));
  p=policy(); p.maxAgeUs=0; WINDOW_CHECK(!g.reset(1,p));
  p=policy(); p.maxAgeUs=0x7fffffffu; WINDOW_CHECK(!g.reset(1,p));
  p=policy(); p.calibrationId[0]=0; WINDOW_CHECK(!g.reset(1,p));
  WINDOW_CHECK(g.reset(1,policy()));
  auto r=g.evaluate(f,s,c,17000); WINDOW_CHECK(r.available() && r.rmsAmps == 1.0);
  WINDOW_CHECK(r.ageUs == 1000 && r.calibrationId == 9 && r.calibrationRevision == 2);
  WINDOW_CHECK(r.timingEvidenceId == 7 && r.session == 1);
  r=g.evaluate(f,s,c,17000); WINDOW_CHECK(r.faults == WindowSequence && isnan(r.rmsAmps));
  auto next=stamp(0,2); r=g.evaluate(f,next,c,17000);
  WINDOW_CHECK(r.faults == WindowGap && isnan(r.rmsAmps));
  next.sequence=3; WINDOW_CHECK(g.evaluate(f,next,c,17000).available());
  auto c1=calibration(1); c1.ampsPerCode=0.02;
  auto c2=calibration(2); c2.ampsPerCode=0.03;
  WINDOW_CHECK(g.evaluate(f,stamp(1),c1,17000).rmsAmps == 2.0);
  WINDOW_CHECK(g.evaluate(f,stamp(2),c2,17000).rmsAmps == 3.0);
  auto wrong=s; wrong.session=2;
  WINDOW_CHECK(g.evaluate(f,wrong,c,17000).faults == WindowSession);
  wrong=s; wrong.channel=3; WINDOW_CHECK(g.evaluate(f,wrong,c,17000).faults == WindowChannel);
  WINDOW_CHECK(!g.reset(1,policy())); // Same-session reset revokes state, cannot allow replay.
  WINDOW_CHECK(g.evaluate(f,s,c,17000).faults == WindowConfig);
  WINDOW_CHECK(g.reset(2,policy()));
  WINDOW_CHECK(g.evaluate(f,s,c,17000).faults == WindowSession);
  wrong=s; wrong.session=2; WINDOW_CHECK(g.evaluate(f,wrong,c,17000).available());
  // End-of-counter wrap never becomes a new sequence in the same session.
  wrong.sequence=UINT64_MAX; WINDOW_CHECK(g.evaluate(f,wrong,c,17000).faults == WindowGap);
  wrong.sequence=0; WINDOW_CHECK(g.evaluate(f,wrong,c,17000).faults == WindowSequence);
  auto expired=once(f,s,c,21001); WINDOW_CHECK(expired.faults == WindowAge && isnan(expired.rmsAmps));
  WINDOW_CHECK(once(f,s,c,21000).available());
  WINDOW_CHECK(once(f,s,c,15999).faults == WindowAge); // Future end timestamp.
  auto wrap=s; wrap.firstUs=UINT32_MAX-14999; wrap.lastUs=0;
  WINDOW_CHECK(once(f,wrap,c,1000).available());
  for (int kind=0;kind<2;++kind) {
    auto bad=s; bad.kind=kind ? SampleTimeKind::CallBracket : SampleTimeKind::Unknown;
    WINDOW_CHECK(once(f,bad,c).faults == WindowTiming);
  }
  for (int test=0;test<6;++test) {
    auto bad=s;
    if(test==0) bad.timingEvidenceId=0;
    if(test==1) bad.intervalsObserved=14;
    if(test==2) bad.minIntervalUs=989;
    if(test==3) bad.maxIntervalUs=1011;
    if(test==4) bad.firstUs=1001; // Span contradicts 15 exactly 1000-us gaps.
    if(test==5) bad.maxIntervalUs=999;
    WINDOW_CHECK(once(f,bad,c).faults == WindowTiming);
  }
  for (int test=0;test<9;++test) {
    auto bad=f;
    if(test==0) bad.faults=FeatureSource;
    if(test==1) bad.codeMax=16383;
    if(test==2) bad.sampleCount=15;
    if(test==3) bad.attempts=17;
    if(test==4) bad.nearRailCount=1;
    if(test==5) bad.meanCode=NAN;
    if(test==6) bad.acRmsCode=INFINITY;
    if(test==7) bad.meanCode=3000;
    if(test==8) bad.acRmsCode=101;
    WINDOW_CHECK(once(bad,s,c).faults & WindowFeatures);
    WINDOW_CHECK(isnan(once(bad,s,c).rmsAmps));
  }
  CurrentCalibration empty;
  WINDOW_CHECK(once(f,s,empty).faults == WindowCalibration);
  for (int test=0;test<18;++test) {
    auto bad=c;
    if(test==0) bad.schema=2;
    if(test==1) bad.recordId=0;
    if(test==2) bad.revision=0;
    if(test==3) bad.evidenceId=0;
    if(test==4) bad.benchQualified=false;
    if(test==5) bad.channel=1;
    if(test==6) bad.codeMax=16383;
    if(test==7) bad.ampsPerCode=0;
    if(test==8) bad.ampsPerCode=NAN;
    if(test==9) bad.ampsPerCode=DBL_MAX;
    if(test==10) bad.minRmsCode=-1;
    if(test==11) bad.maxRmsCode=3000;
    if(test==12) bad.ampsPerCode=-1;
    if(test==13) bad.maxRmsCode=NAN;
    if(test==14) bad.minRmsCode=bad.maxRmsCode;
    if(test==15) bad.ampsPerCode=INFINITY;
    if(test==16) bad.recordId=99;
    if(test==17) bad.revision=1;
    WINDOW_CHECK(once(f,s,bad).faults == WindowCalibration);
  }
  auto low=c; low.minRmsCode=101; WINDOW_CHECK(once(f,s,low).faults == WindowRange);
  auto high=c; high.maxRmsCode=99; WINDOW_CHECK(once(f,s,high).faults == WindowRange);
  low.minRmsCode=100; WINDOW_CHECK(once(f,s,low).available());
  high.maxRmsCode=100; WINDOW_CHECK(once(f,s,high).available());
  RawFeatureAccumulator tiny; tiny.reset({4095,8,16,16});
  for(int i=0;i<16;++i) tiny.add(i ? 1000 : 1001);
  auto subnormal=c; subnormal.ampsPerCode=DBL_MIN*DBL_EPSILON;
  subnormal.minRmsCode=0; subnormal.maxRmsCode=1;
  WINDOW_CHECK(once(tiny.finish(true),s,subnormal).faults == WindowNumeric);
  // Invalid quality consumes the ordinal; re-evaluating cannot resurrect it.
  CurrentWindowGate consumed; WINDOW_CHECK(consumed.reset(1,policy()));
  auto bad=f; bad.faults=FeatureRail;
  WINDOW_CHECK(consumed.evaluate(bad,s,c,17000).faults & WindowFeatures);
  WINDOW_CHECK(consumed.evaluate(f,s,c,17000).faults == WindowSequence);
  WINDOW_CHECK(consumed.evaluate(f,stamp(0,1),c,17000).available());
  CurrentWindowGate disabled; p=policy(); p.calibrationId[0]=p.calibrationRevision[0]=0;
  WINDOW_CHECK(disabled.reset(1,p));
  WINDOW_CHECK(disabled.evaluate(f,s,c,17000).faults == WindowCalibration);
  CurrentWindowGate revision; WINDOW_CHECK(revision.reset(1,policy()));
  WINDOW_CHECK(revision.evaluate(f,s,c,17000).available());
  p=policy(); p.calibrationRevision[0]=3; WINDOW_CHECK(revision.reset(2,p));
  auto s2=s; s2.session=2;
  WINDOW_CHECK(revision.evaluate(f,s2,c,17000).faults == WindowCalibration);
  auto c3=c; c3.revision=3; s2.sequence=1;
  WINDOW_CHECK(revision.evaluate(f,s2,c3,17000).available());
  return 0;
#undef WINDOW_CHECK
}
} // namespace hvac_window_test
