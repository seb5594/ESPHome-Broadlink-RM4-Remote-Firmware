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

/// Sends the raw timings Home Assistant hands over through the module. Only a
/// transmitter: received codes already reach Home Assistant as events, and
/// Home Assistant's radio frequency platform does not take receivers yet.
class RadioFrequencyEntity : public radio_frequency::RadioFrequency, public Parented<BL3361Component> {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  bool control(const radio_frequency::RadioFrequencyCall &call) override;
};

}  // namespace esphome::bl3361

#endif  // USE_RADIO_FREQUENCY
#endif  // __has_include
