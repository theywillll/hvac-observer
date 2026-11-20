#pragma once
#include <RawSequence.h>
#include <SequenceFeatures.h>
inline int runSequenceTests() {
#define SEQ_CHECK(x) do { if (!(x)) return __LINE__; } while (false)
  hvac_current::RawSequence s;
  SEQ_CHECK(s.guardsOk() && !s.publishable(0));
  s.prepare(); SEQ_CHECK(s.guardsOk()); SEQ_CHECK(!s.publishable(0));
  for (size_t i = 0; i < s.Count; ++i) s.codes[i] = uint16_t(i);
  SEQ_CHECK(s.publishable(0)); SEQ_CHECK(!s.publishable(-5));
  SEQ_CHECK(!s.publishable(-16)); SEQ_CHECK(!s.publishable(1));
  s.codes[0] = 0; s.codes[47] = 16383; SEQ_CHECK(s.publishable(0));
  s.codes[46] = 16384; SEQ_CHECK(!s.publishable(0)); s.codes[46] = 2048;
  s.before = 0; SEQ_CHECK(!s.guardsOk() && !s.publishable(0));
  s.before = s.Guard; s.after = 0; SEQ_CHECK(!s.publishable(0));
  s.prepare(); SEQ_CHECK(s.guardsOk() && !s.publishable(0));
  for (size_t i = 0; i < s.Count - 1; ++i) s.codes[i] = 2048;
  SEQ_CHECK(!s.publishable(0)); // A successful return cannot hide an unwritten cell.
  hvac_current::RawFeatures channels[3];
  hvac_current::sequenceFeatures(s, 0, channels);
  for (const auto& channel : channels)
    SEQ_CHECK(channel.faults & hvac_current::FeatureSource);
  for (size_t sweep = 0; sweep < s.Sweeps; ++sweep) {
    s.codes[3*sweep] = uint16_t(sweep % 2 ? 8100 : 7900);
    s.codes[3*sweep+1] = 6000;
    s.codes[3*sweep+2] = uint16_t(sweep % 2 ? 12020 : 11980);
  }
  hvac_current::sequenceFeatures(s, 0, channels);
  SEQ_CHECK(channels[0].meanCode == 8000 && channels[0].acRmsCode == 100);
  SEQ_CHECK(channels[1].meanCode == 6000 && channels[1].acRmsCode == 0);
  SEQ_CHECK(channels[2].meanCode == 12000 && channels[2].acRmsCode == 20);
  s.codes[1] = 0;
  hvac_current::sequenceFeatures(s, 0, channels);
  SEQ_CHECK(channels[0].statisticsAvailable() && channels[2].statisticsAvailable());
  SEQ_CHECK(channels[1].nearRailCount == 1 && isnan(channels[1].acRmsCode));
  hvac_current::sequenceFeatures(s, -16, channels);
  for (const auto& channel : channels)
    SEQ_CHECK((channel.faults & hvac_current::FeatureSource) && isnan(channel.meanCode));
  return 0;
#undef SEQ_CHECK
}
