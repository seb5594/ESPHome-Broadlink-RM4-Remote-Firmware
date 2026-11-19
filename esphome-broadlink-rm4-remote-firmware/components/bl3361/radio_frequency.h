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

/// The module as one side of Home Assistant's radio frequency platform. Home
/// Assistant (and the ESPHome documentation) treat an entity as either a
/// transmitter or a receiver, never both: an entity that claims both ends up as a
/// receiver only. So the component creates two of them for the one module.
///
///   transmitter  sends the raw timings Home Assistant hands over
///   receiver     passes every transmission the module receives on to the API as
///                a receive event, the same ones that fire `on_receive` of the
///                component. Home Assistant creates the entity for it as soon as
///                its integration supports receivers
class RadioFrequencyEntity : public radio_frequency::RadioFrequency, public Parented<BL3361Component> {
 public:
  void setup() override;
  void dump_config() override;
  /// Declare one fixed frequency instead of the tuning range of the module.
  void set_fixed_frequency_hz(uint32_t frequency_hz) { this->fixed_frequency_hz_ = frequency_hz; }
  /// Makes this the receiving side. Without it the entity is the transmitting side.
  void set_receiver(bool receiver) { this->receiver_ = receiver; }

 protected:
  bool control(const radio_frequency::RadioFrequencyCall &call) override;

  uint32_t fixed_frequency_hz_{0};
  bool receiver_{false};
};

}  // namespace esphome::bl3361

#endif  // USE_RADIO_FREQUENCY
#endif  // __has_include
