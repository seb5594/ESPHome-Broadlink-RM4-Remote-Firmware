#include "frequency_select.h"

#include "esphome/core/log.h"

#include "bl3361.h"

namespace esphome::bl3361 {

static const char *const TAG = "bl3361.select";

void FrequencySelect::show(uint32_t frequency_khz) {
  const int index = find_option(this->options_khz_, frequency_khz);
  // The last option is "Custom".
  this->publish_state(index >= 0 ? static_cast<size_t>(index) : this->options_khz_.size());
}

void FrequencySelect::control(size_t index) {
  if (index >= this->options_khz_.size()) {
    // "Custom" only reports; go back to what the radio really uses.
    this->show(this->parent_->frequency_khz());
    return;
  }
  if (!this->parent_->set_frequency(this->options_khz_[index])) {
    ESP_LOGW(TAG, "Could not switch to %.3f MHz", this->options_khz_[index] / 1e3f);
    this->show(this->parent_->frequency_khz());
    return;
  }
  this->show(this->parent_->frequency_khz());
}

}  // namespace esphome::bl3361
