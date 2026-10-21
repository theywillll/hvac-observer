#pragma once
#include <CurrentWindow.h>

namespace hvac_current {
struct CurrentSnapshot {
  uint64_t session = 0, requestedUs = 0;
  uint32_t maxAgeUs = 0;
  CurrentWindow channels[3];
  bool observed[3] = {};
};

// Fixed storage, single owner. Serialize ALL calls externally before adding a
// worker. No ADC, clock extension, JSON, Bridge, I2C or synchronization is hidden
// here. Callers supply a trustworthy extended monotonic microsecond clock.
class CurrentSnapshotStore {
 public:
  CurrentSnapshotStore() { clear(WindowConfig); }
  CurrentSnapshotStore(const CurrentSnapshotStore&) = delete;
  CurrentSnapshotStore& operator=(const CurrentSnapshotStore&) = delete;

  bool reset(uint64_t session, WindowPolicy policy, uint64_t nowUs) {
    active_ = false; session_ = 0; maxAgeUs_ = 0;
    clear(WindowConfig);
    if (!gate_.reset(session, policy)) return false;
    session_ = session; maxAgeUs_ = policy.maxAgeUs; lastNowUs_ = nowUs;
    active_ = true; clear(WindowPending);
    return true;
  }

  // A stop requires a strictly newer session before publication can resume.
  void stop() { active_ = false; invalidate(WindowStopped); }

  // lastSampleUs is the SAME sample-end time as stamp.lastUs, extended into the
  // monotonic 64-bit domain. Request/callback completion is not a substitute.
  // True means the active session/channel was processed, not that it was good.
  bool publish(const RawFeatures& features, const WindowStamp& stamp,
               const CurrentCalibration& calibration, uint64_t lastSampleUs,
               uint64_t nowUs) {
    if (!active_ || stamp.session != session_ || stamp.channel >= 3) return false;
    if (!advanceClock(nowUs)) return false;
    const unsigned c = stamp.channel;
    CurrentWindow out = gate_.evaluate(features, stamp, calibration, uint32_t(nowUs));
    if (uint32_t(lastSampleUs) != stamp.lastUs) out.faults |= WindowTiming;
    ends_[c] = lastSampleUs; observed_[c] = true;
    updateAge(out, lastSampleUs, nowUs);
    if (out.faults) out.rmsAmps = NAN;
    latest_[c] = out; // Invalid/duplicate windows revoke the previous good value.
    return true;
  }

  // Recompute age at the point of use. A returned copy is only valid at this
  // request time; do not retain it as an indefinitely fresh frame.
  CurrentSnapshot snapshot(uint64_t nowUs) {
    if (active_) advanceClock(nowUs);
    CurrentSnapshot out;
    out.session = session_; out.maxAgeUs = maxAgeUs_; out.requestedUs = nowUs;
    for (unsigned c=0;c<3;++c) {
      if (active_ && observed_[c]) updateAge(latest_[c], ends_[c], nowUs);
      out.channels[c] = latest_[c];
      out.observed[c] = observed_[c];
    }
    return out;
  }

 private:
  void clear(uint32_t fault) {
    for (unsigned c=0;c<3;++c) {
      latest_[c] = CurrentWindow{}; latest_[c].faults = fault;
      latest_[c].channel = uint8_t(c); latest_[c].session = session_;
      ends_[c] = 0; observed_[c] = false;
    }
  }
  void invalidate(uint32_t fault) {
    for (unsigned c=0;c<3;++c) {
      latest_[c].faults |= fault; latest_[c].rmsAmps = NAN;
    }
  }
  bool advanceClock(uint64_t nowUs) {
    if (nowUs < lastNowUs_) {
      active_ = false; invalidate(WindowClock); return false;
    }
    lastNowUs_ = nowUs; return true;
  }
  void updateAge(CurrentWindow& value, uint64_t endUs, uint64_t nowUs) {
    if (endUs > nowUs) {
      value.ageUs = UINT32_MAX; value.faults |= WindowAge;
    } else {
      const uint64_t age = nowUs-endUs;
      value.ageUs = age >= UINT32_MAX ? UINT32_MAX : uint32_t(age);
      if (age > maxAgeUs_) value.faults |= WindowAge;
    }
    // Age failures latch until a fresh publication; modulo rollover cannot
    // resurrect an old current value, even after an entire skipped clock cycle.
    if (value.faults) value.rmsAmps = NAN;
  }
  CurrentWindowGate gate_;
  CurrentWindow latest_[3];
  uint64_t ends_[3] = {}, session_ = 0, lastNowUs_ = 0;
  uint32_t maxAgeUs_ = 0;
  bool observed_[3] = {}, active_ = false;
};
} // namespace hvac_current
