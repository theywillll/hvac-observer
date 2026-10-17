#pragma once
#include <RawSequence.h>
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
  return 0;
#undef SEQ_CHECK
}
