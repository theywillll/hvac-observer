#pragma once
#include <RawSequence.h>
#include <RawFeatures.h>

namespace hvac_current {
// Demultiplex native ADC-ID order. Timing qualification belongs to the caller.
inline void sequenceFeatures(const RawSequence& capture, int driverStatus,
                             RawFeatures (&channels)[RawSequence::Channels]) {
  const bool usable = capture.publishable(driverStatus);
  for (size_t channel = 0; channel < RawSequence::Channels; ++channel) {
    RawFeatureAccumulator accumulator;
    accumulator.reset({RawSequence::CodeMax, 32, RawSequence::Sweeps, RawSequence::Sweeps});
    if (usable) {
      for (size_t sweep = 0; sweep < RawSequence::Sweeps; ++sweep) {
        accumulator.add(capture.codes[sweep * RawSequence::Channels + channel]);
      }
    }
    channels[channel] = accumulator.finish(usable);
  }
}
} // namespace hvac_current
