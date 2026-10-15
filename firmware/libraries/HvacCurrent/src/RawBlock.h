#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hvac_current {
enum Fault : uint32_t {
  Incomplete = 1u, Order = 2u, DriverError = 4u, InvalidCode = 8u,
  NearRail = 16u, Timing = 32u, Aborted = 64u
};
struct TimingLimits {
  uint32_t minIntervalUs;
  uint32_t maxIntervalUs;
  uint32_t maxReadUs;
  uint16_t codeMax;
  uint16_t railMargin;
  bool valid() const {
    return minIntervalUs > 0 && maxIntervalUs >= minIntervalUs &&
           maxIntervalUs < 0x80000000u && maxReadUs > 0 &&
           maxReadUs < 0x80000000u && codeMax > 0 &&
           uint32_t(railMargin) * 2 < codeMax;
  }
};
struct ChannelTiming {
  uint32_t count = 0, firstUs = 0, lastUs = 0;
  uint32_t minIntervalUs = UINT32_MAX, maxIntervalUs = 0, maxReadUs = 0;
  uint32_t nearRailCount = 0;
};

// One owner must serialize ALL calls, including reads and reset. Not ISR-safe
// or a lock-free queue. A future worker must supply synchronization externally.
template <size_t Samples, size_t Slots = 2> class RawBlockPool {
  static_assert(Samples >= 2 && Samples <= 65535, "Invalid block length");
  static_assert(Slots >= 2 && Slots <= 255, "Invalid slot count");
 public:
  RawBlockPool() = default;
  RawBlockPool(const RawBlockPool&) = delete;
  RawBlockPool& operator=(const RawBlockPool&) = delete;
  struct Block {
    uint16_t codes[3][Samples] = {};
    ChannelTiming channels[3];
    uint64_t ticket = 0;
    uint32_t session = 0, faults = 0;
    uint32_t maxSweepSkewUs = 0;
    int32_t firstDriverError = 0;
    bool usable() const {
      return faults == 0 && channels[0].count == Samples &&
             channels[1].count == Samples && channels[2].count == Samples;
    }
  };
  struct Lease {
    uint8_t slot = 255;
    uint64_t ticket = 0;
  };
  // A reset revokes every lease and clears partial/ready blocks and counters.
  // Tickets are never reset/reused during this pool object's lifetime.
  bool reset(uint32_t session, TimingLimits limits) {
    active_ = -1;
    for (size_t i = 0; i < Slots; ++i) states_[i] = Free;
    overruns_ = 0;
    session_ = session;
    limits_ = limits;
    configured_ = session != 0 && limits.valid();
    return configured_;
  }
  bool begin() {
    if (!configured_ || active_ >= 0 || nextTicket_ == UINT64_MAX) return false;
    for (size_t i = 0; i < Slots; ++i) {
      if (states_[i] != Free) continue;
      blocks_[i] = Block{};
      blocks_[i].ticket = nextTicket_++;
      blocks_[i].session = session_;
      states_[i] = Filling;
      active_ = int(i);
      nextChannel_ = 0;
      havePrevious_ = false;
      return true;
    }
    if (overruns_ != UINT32_MAX) ++overruns_;
    return false; // Never overwrite unread or leased data.
  }
  // Feed one complete sweep in channel order 0,1,2. Timestamp is provided
  // by the backend: call-start time is NOT an ADC aperture timestamp.
  bool append(uint8_t channel, int32_t raw, uint32_t startUs, uint32_t readUs) {
    if (active_ < 0) return false;
    Block& b = blocks_[active_];
    if (channel >= 3 || channel != nextChannel_ ||
        b.channels[channel].count >= Samples) {
      b.faults |= Order;
      return false;
    }
    ChannelTiming& t = b.channels[channel];
    if (havePrevious_) {
      const uint32_t step = startUs - previousStartUs_;
      if (step >= 0x80000000u || step < previousReadUs_) b.faults |= Timing;
    }
    havePrevious_ = true;
    previousStartUs_ = startUs;
    previousReadUs_ = readUs;
    if (channel == 0) sweepStartUs_ = startUs;
    if (channel == 2) {
      const uint32_t skew = startUs - sweepStartUs_;
      if (skew > b.maxSweepSkewUs) b.maxSweepSkewUs = skew;
    }
    if (t.count) {
      const uint32_t delta = startUs - t.lastUs;
      if (delta < t.minIntervalUs) t.minIntervalUs = delta;
      if (delta > t.maxIntervalUs) t.maxIntervalUs = delta;
      if (delta < limits_.minIntervalUs || delta > limits_.maxIntervalUs)
        b.faults |= Timing;
    } else t.firstUs = startUs;
    t.lastUs = startUs;
    if (readUs > t.maxReadUs) t.maxReadUs = readUs;
    if (readUs > limits_.maxReadUs) b.faults |= Timing;
    if (raw < 0) {
      b.faults |= DriverError;
      if (!b.firstDriverError) b.firstDriverError = raw;
    } else if (uint32_t(raw) > limits_.codeMax) b.faults |= InvalidCode;
    else {
      b.codes[channel][t.count] = uint16_t(raw);
      if (raw <= limits_.railMargin ||
          uint32_t(raw) >= uint32_t(limits_.codeMax - limits_.railMargin)) {
        ++t.nearRailCount;
        b.faults |= NearRail;
      }
    }
    ++t.count; // Counts attempted conversions, including explicitly flagged errors.
    nextChannel_ = uint8_t((channel + 1) % 3);
    return true;
  }
  bool finish(bool aborted = false) {
    if (active_ < 0) return false;
    Block& b = blocks_[active_];
    for (size_t c = 0; c < 3; ++c)
      if (b.channels[c].count != Samples) b.faults |= Incomplete;
    if (aborted) b.faults |= Aborted;
    states_[active_] = Ready;
    active_ = -1;
    return true; // Invalid blocks are delivered for reporting, never as valid RMS.
  }
  bool acquire(Lease& lease) {
    int oldest = -1;
    for (size_t i = 0; i < Slots; ++i)
      if (states_[i] == Ready &&
          (oldest < 0 || blocks_[i].ticket < blocks_[oldest].ticket)) oldest = int(i);
    if (oldest < 0) { lease = Lease{}; return false; }
    states_[oldest] = Reading;
    lease.slot = uint8_t(oldest);
    lease.ticket = blocks_[oldest].ticket;
    return true;
  }
  // Pointer expires at release/reset. Revalidate through view after pool calls.
  const Block* view(Lease lease) const {
    if (lease.slot >= Slots || states_[lease.slot] != Reading ||
        blocks_[lease.slot].ticket != lease.ticket) return nullptr;
    return &blocks_[lease.slot];
  }
  bool release(Lease lease) {
    if (!view(lease)) return false;
    states_[lease.slot] = Free;
    return true;
  }
  uint32_t overruns() const { return overruns_; }
 private:
  enum State : uint8_t { Free, Filling, Ready, Reading };
  Block blocks_[Slots];
  State states_[Slots] = {};
  TimingLimits limits_{};
  uint64_t nextTicket_ = 1;
  uint32_t session_ = 0, overruns_ = 0;
  uint32_t previousStartUs_ = 0, previousReadUs_ = 0, sweepStartUs_ = 0;
  int active_ = -1;
  uint8_t nextChannel_ = 0;
  bool configured_ = false;
  bool havePrevious_ = false;
};
} // namespace hvac_current
