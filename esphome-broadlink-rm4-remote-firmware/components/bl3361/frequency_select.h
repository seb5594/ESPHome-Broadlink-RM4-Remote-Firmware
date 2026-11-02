#pragma once

#include <vector>

#include "esphome/components/select/select.h"
#include "esphome/core/helpers.h"

#include "frequency_options.h"

namespace esphome::bl3361 {

class BL3361Component;

/// The frequency of the radio as a choice in Home Assistant.
///
/// The options are the frequencies of the configuration plus a last one,
/// "Custom", which is shown while the module sits on a frequency that is not
/// in the list (after a search found one, for example). Choosing "Custom" does
/// nothing.
class FrequencySelect : public select::Select, public Parented<BL3361Component> {
 public:
  /// The frequency of every option except the last one, in kHz.
  void set_options_khz(std::vector<uint32_t> options_khz) { this->options_khz_ = std::move(options_khz); }

  /// Shows `frequency_khz` as the selected option.
  void show(uint32_t frequency_khz);

 protected:
  void control(size_t index) override;

  std::vector<uint32_t> options_khz_;
};

}  // namespace esphome::bl3361
