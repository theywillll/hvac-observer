#pragma once
#include <math.h>
#include <stdint.h>

namespace hvac_current {
enum FeatureFault : uint32_t {
  FeatureBadConfig = 1u, FeatureSource = 2u, FeatureShort = 4u,
  FeatureCode = 8u, FeatureRail = 16u, FeatureOverflow = 32u
};
struct FeatureConfig {
  uint16_t codeMax;
  uint16_t railMargin;
  uint32_t minSamples;
  uint32_t maxSamples;
  bool valid() const {
    return codeMax > 0 && uint32_t(railMargin) * 2 < codeMax &&
           minSamples >= 2 && maxSamples >= minSamples && maxSamples <= 65535;
  }
};
struct RawFeatures {
  uint32_t faults = FeatureBadConfig, attempts = 0, sampleCount = 0, nearRailCount = 0;
  uint16_t minCode = 0, maxCode = 0;
  uint16_t codeMax = 0;
  double meanCode = NAN, acRmsCode = NAN;
  bool statisticsAvailable() const { return faults == 0; }
};

// Mathematical statistics only. No timing, ADC reference, CT scale or amperes.
// One owner per accumulator; reset once per new channel/window/session.
class RawFeatureAccumulator {
 public:
  bool reset(FeatureConfig config) {
    config_ = config;
    sum_ = sumSquares_ = 0;
    attempts_ = count_ = rails_ = 0;
    min_ = UINT16_MAX; max_ = 0;
    configured_ = config.valid();
    faults_ = configured_ ? 0u : uint32_t(FeatureBadConfig);
    return configured_;
  }
  bool add(int32_t raw) {
    if (!configured_) return false;
    if (attempts_ >= config_.maxSamples) { faults_ |= FeatureOverflow; return false; }
    ++attempts_;
    if (raw < 0 || uint32_t(raw) > config_.codeMax) { faults_ |= FeatureCode; return false; }
    const uint16_t code = uint16_t(raw);
    if (code <= config_.railMargin || code >= config_.codeMax - config_.railMargin) {
      ++rails_; faults_ |= FeatureRail;
    }
    if (code < min_) min_ = code;
    if (code > max_) max_ = code;
    ++count_;
    sum_ += code;
    sumSquares_ += uint64_t(code) * code;
    return true; // In-range numeric observation; inspect finish() for rail/source faults.
  }
  RawFeatures finish(bool sourceCompleteAndUsable = false) const {
    RawFeatures out;
    out.faults = faults_;
    if (!sourceCompleteAndUsable) out.faults |= FeatureSource;
    if (configured_ && count_ < config_.minSamples) out.faults |= FeatureShort;
    out.attempts = attempts_; out.sampleCount = count_; out.nearRailCount = rails_;
    out.codeMax = configured_ ? config_.codeMax : 0;
    if (count_) { out.minCode = min_; out.maxCode = max_; }
    if (out.faults) return out; // Numeric outputs stay NaN, never stale or false zero.
    // Exact integer centering avoids cancellation of a small ripple on large DC.
    // count,code <= 65535 => both products <= 65535^4 < UINT64_MAX.
    const uint64_t centered = uint64_t(count_) * sumSquares_ - sum_ * sum_;
    out.meanCode = double(sum_) / count_;
    out.acRmsCode = sqrt(double(centered)) / count_; // Population RMS, divisor N.
    return out;
  }
 private:
  FeatureConfig config_{};
  uint64_t sum_ = 0, sumSquares_ = 0;
  uint32_t attempts_ = 0, count_ = 0, rails_ = 0, faults_ = FeatureBadConfig;
  uint16_t min_ = UINT16_MAX, max_ = 0;
  bool configured_ = false;
};
} // namespace hvac_current
