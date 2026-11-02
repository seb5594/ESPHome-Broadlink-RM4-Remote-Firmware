#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"

namespace esphome::bl3361 {

class BL3361Component;

/// The frequency of the radio as a number in MHz, for any frequency the module
/// can tune to: 305-340 MHz and 430-440 MHz. Where the select entity offers a
/// list, this one takes what you type, for example 433.42.
class FrequencyNumber : public number::Number, public Parented<BL3361Component> {
 public:
  /// Shows `frequency_khz` as the value.
  void show(uint32_t frequency_khz);

 protected:
  void control(float mhz) override;
};

}  // namespace esphome::bl3361
