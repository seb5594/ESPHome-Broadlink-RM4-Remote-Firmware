#pragma once

// The RF module as a native `radio_frequency` entity of Home Assistant, the
// entity type for sub-GHz transmitters that other integrations use to send.
//
// ESPHome marks the radio_frequency component as experimental, and its
// interface changed between 2026.9 and 2026.10 (ir_rf_base appeared). This
// file is written for the newer one and compiles to nothing on an older ESPHome.

#if __has_include("esphome/components/ir_rf_base/ir_rf_base.h")

#include "esphome/components/radio_frequency/radio_frequency.h"
#include "esphome/core/helpers.h"

#ifdef USE_RADIO_FREQUENCY

namespace esphome::bl3361 {

class BL3361Component;

/// The module as transmitter and receiver of Home Assistant's radio frequency
/// platform. It sends the raw timings Home Assistant hands over, and it passes
/// every transmission the module receives on to the API as a receive event, the
/// same ones that fire `on_receive` of the component. Home Assistant creates the
/// entity of a receiver from them as soon as its integration supports it.
class RadioFrequencyEntity : public radio_frequency::RadioFrequency, public Parented<BL3361Component> {
 public:
  void setup() override;
  void dump_config() override;
  /// Declare one fixed frequency instead of the tuning range of the module.
  void set_fixed_frequency_hz(uint32_t frequency_hz) { this->fixed_frequency_hz_ = frequency_hz; }

 protected:
  bool control(const radio_frequency::RadioFrequencyCall &call) override;

  uint32_t fixed_frequency_hz_{0};
};

}  // namespace esphome::bl3361

#endif  // USE_RADIO_FREQUENCY
#endif  // __has_include
