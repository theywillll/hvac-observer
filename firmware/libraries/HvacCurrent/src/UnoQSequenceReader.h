#pragma once
#if !defined(ARDUINO_UNO_Q) || !defined(ARDUINO_ARCH_ZEPHYR)
#error "UNO Q sequence reader requires arduino:zephyr:unoq"
#endif
#include <zephyr/drivers/adc.h>
#include <zephyrPinctrl.h>
#include <RawSequence.h>
#include <errno.h>

namespace hvac_current {
// One thread owns setup, buffer and reads. Blocking adc_read must run outside
// the slow I2C and Bridge loop. The caller brackets calls for diagnostics.
class UnoQSequenceReader {
 public:
  UnoQSequenceReader() = default;
  UnoQSequenceReader(const UnoQSequenceReader&) = delete;
  UnoQSequenceReader& operator=(const UnoQSequenceReader&) = delete;

  int initialize() {
    if (attempted_) return setupStatus_;
    attempted_ = true;
    setupStatus_ = configureInputs();
    return setupStatus_;
  }

  int read(RawSequence& capture, uint32_t requestedIntervalUs) {
    if (setupStatus_ != 0) return setupStatus_;
    if (damaged_ || !capture.guardsOk()) {
      damaged_ = true;
      return -EFAULT;
    }
    capture.prepare();
    adc_sequence_options options{};
    options.interval_us = requestedIntervalUs;
    options.extra_samplings = RawSequence::Sweeps - 1;
    adc_sequence sequence{};
    sequence.options = &options;
    sequence.channels = BIT(9) | BIT(10) | BIT(11);
    sequence.buffer = capture.codes;
    sequence.buffer_size = sizeof(capture.codes);
    sequence.resolution = inputs_[0].resolution;
    sequence.oversampling = inputs_[0].oversampling;
    const int status = adc_read(inputs_[0].dev, &sequence);
    damaged_ = !capture.guardsOk();
    return status;
  }

 private:
  int configureInputs() {
    for (size_t i = 0; i < RawSequence::Channels; ++i) {
      if (inputs_[i].dev != inputs_[0].dev || inputs_[i].channel_id != 9 + i ||
          inputs_[i].resolution != 14 || inputs_[i].oversampling != inputs_[0].oversampling ||
          inputs_[i].channel_cfg.differential) return -ENOTSUP;
      int status = zephyr::arduino::init_dev_apply_channel_pinctrl(
          inputs_[i].dev, zephyr::arduino::state_pin_index_from_spec_index(inputs_, i));
      if (status != 0) return status;
      if (!adc_is_ready_dt(&inputs_[i])) return -ENODEV;
      status = adc_channel_setup_dt(&inputs_[i]);
      if (status != 0) return status;
    }
    return 0;
  }

  const adc_dt_spec inputs_[RawSequence::Channels] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2)
  };
  int setupStatus_ = -ENODEV;
  bool attempted_ = false, damaged_ = false;
};

static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 0) == 9, "A0 mapping changed");
static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 1) == 10, "A1 mapping changed");
static_assert(DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 2) == 11, "A2 mapping changed");
} // namespace hvac_current
