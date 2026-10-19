#pragma once
#include <RawFeatures.h>

namespace hvac_current {
enum WindowFault : uint32_t {
  WindowConfig = 1u, WindowSession = 2u, WindowChannel = 4u,
  WindowSequence = 8u, WindowGap = 16u, WindowFeatures = 32u,
  WindowTiming = 64u, WindowAge = 128u, WindowCalibration = 256u,
  WindowRange = 512u, WindowNumeric = 1024u
};
enum class SampleTimeKind : uint8_t { Unknown, CallBracket, QualifiedAperture };
struct WindowPolicy {
  uint32_t samples = 0, minIntervalUs = 0, maxIntervalUs = 0, maxAgeUs = 0;
  uint16_t codeMax = 0;
  uint32_t calibrationId[3] = {}, calibrationRevision[3] = {};
  bool valid() const {
    for (unsigned c=0;c<3;++c)
      if ((calibrationId[c] == 0) != (calibrationRevision[c] == 0)) return false;
    return samples >= 2 && samples <= 65535 && codeMax > 0 &&
           minIntervalUs > 0 && maxIntervalUs >= minIntervalUs &&
           uint64_t(samples-1)*maxIntervalUs + maxAgeUs < 0x80000000u &&
           maxAgeUs > 0 && maxAgeUs < 0x80000000u;
  }
};
struct WindowStamp {
  uint64_t session = 0, sequence = 0;
  uint8_t channel = 255;
  SampleTimeKind kind = SampleTimeKind::Unknown;
  uint32_t timingEvidenceId = 0, firstUs = 0, lastUs = 0;
  uint32_t intervalsObserved = 0, minIntervalUs = 0, maxIntervalUs = 0;
};
// Schema 1: measured total chain gain, A / raw ADC code, no assumed Vref/CT.
// Bench flags/IDs assert external evidence; this object cannot create that evidence.
struct CurrentCalibration {
  uint32_t schema = 0, recordId = 0, revision = 0, evidenceId = 0;
  uint8_t channel = 255;
  uint16_t codeMax = 0;
  bool benchQualified = false;
  double ampsPerCode = NAN, minRmsCode = NAN, maxRmsCode = NAN;
};
struct CurrentWindow {
  uint32_t faults = WindowConfig, ageUs = UINT32_MAX;
  uint64_t session = 0, sequence = 0;
  uint8_t channel = 255;
  uint32_t timingEvidenceId = 0, calibrationId = 0, calibrationRevision = 0;
  double rmsAmps = NAN;
  bool available() const { return faults == 0; }
};

// Single owner: reset/evaluate must be serialized. No hardware or frame output.
class CurrentWindowGate {
 public:
  CurrentWindowGate() = default;
  CurrentWindowGate(const CurrentWindowGate&) = delete;
  CurrentWindowGate& operator=(const CurrentWindowGate&) = delete;
  bool reset(uint64_t session, WindowPolicy policy) {
    configured_ = false;
    for (unsigned c=0;c<3;++c) { seen_[c] = false; lastSequence_[c] = 0; }
    if (session == 0 || session <= session_ || !policy.valid()) return false;
    session_ = session; policy_ = policy; configured_ = true;
    return true;
  }
  CurrentWindow evaluate(const RawFeatures& features, const WindowStamp& stamp,
                         const CurrentCalibration& calibration, uint32_t nowUs) {
    CurrentWindow out;
    out.session = stamp.session; out.sequence = stamp.sequence; out.channel = stamp.channel;
    out.timingEvidenceId = stamp.timingEvidenceId;
    out.faults = 0;
    if (!configured_) { out.faults = WindowConfig; return out; }
    if (stamp.session != session_) { out.faults = WindowSession; return out; }
    if (stamp.channel >= 3) { out.faults = WindowChannel; return out; }
    const unsigned c = stamp.channel;
    if (seen_[c] && (lastSequence_[c] == UINT64_MAX || stamp.sequence <= lastSequence_[c])) {
      out.faults = WindowSequence; return out;
    }
    if ((!seen_[c] && stamp.sequence != 0) ||
        (seen_[c] && stamp.sequence != lastSequence_[c]+1)) out.faults |= WindowGap;
    // A correctly addressed new ordinal is consumed even if its data is bad.
    // A gap invalidates this window; a subsequent contiguous window may recover.
    seen_[c] = true; lastSequence_[c] = stamp.sequence;
    const bool numericFeatures = isfinite(features.meanCode) && isfinite(features.acRmsCode);
    if (!features.statisticsAvailable() || features.codeMax != policy_.codeMax ||
        features.sampleCount != policy_.samples || features.attempts != features.sampleCount ||
        features.nearRailCount != 0 || features.minCode > features.maxCode ||
        features.maxCode > policy_.codeMax || !numericFeatures ||
        features.meanCode < features.minCode || features.meanCode > features.maxCode ||
        features.acRmsCode < 0 ||
        features.acRmsCode > (features.maxCode-features.minCode)*0.5+1e-9)
      out.faults |= WindowFeatures;
    const uint32_t span = stamp.lastUs - stamp.firstUs;
    const uint64_t intervals = policy_.samples-1;
    if (stamp.kind != SampleTimeKind::QualifiedAperture || stamp.timingEvidenceId == 0 ||
        stamp.intervalsObserved != intervals || stamp.minIntervalUs < policy_.minIntervalUs ||
        stamp.maxIntervalUs > policy_.maxIntervalUs || stamp.maxIntervalUs < stamp.minIntervalUs ||
        span >= 0x80000000u || span < intervals*stamp.minIntervalUs ||
        span > intervals*stamp.maxIntervalUs) out.faults |= WindowTiming;
    out.ageUs = nowUs - stamp.lastUs;
    if (out.ageUs >= 0x80000000u || out.ageUs > policy_.maxAgeUs) out.faults |= WindowAge;
    if (calibration.schema != 1 || calibration.recordId == 0 || calibration.revision == 0 ||
        calibration.evidenceId == 0 || !calibration.benchQualified ||
        calibration.recordId != policy_.calibrationId[c] ||
        calibration.revision != policy_.calibrationRevision[c] ||
        calibration.channel != c || calibration.codeMax != policy_.codeMax ||
        !isfinite(calibration.ampsPerCode) || calibration.ampsPerCode <= 0 ||
        !isfinite(calibration.minRmsCode) || !isfinite(calibration.maxRmsCode) ||
        calibration.minRmsCode < 0 || calibration.maxRmsCode <= calibration.minRmsCode ||
        calibration.maxRmsCode > policy_.codeMax*0.5 ||
        !isfinite(calibration.ampsPerCode*calibration.maxRmsCode) ||
        calibration.ampsPerCode*calibration.maxRmsCode == 0) {
      out.faults |= WindowCalibration;
    } else {
      out.calibrationId = calibration.recordId; out.calibrationRevision = calibration.revision;
      if (features.acRmsCode < calibration.minRmsCode || features.acRmsCode > calibration.maxRmsCode)
        out.faults |= WindowRange;
    }
    if (out.faults) return out;
    const double amps = features.acRmsCode*calibration.ampsPerCode;
    if (!isfinite(amps) || (features.acRmsCode > 0 && amps == 0)) {
      out.faults = WindowNumeric; return out;
    }
    out.rmsAmps = amps;
    return out;
  }
 private:
  WindowPolicy policy_{};
  uint64_t session_ = 0, lastSequence_[3] = {};
  bool configured_ = false, seen_[3] = {};
};
} // namespace hvac_current
