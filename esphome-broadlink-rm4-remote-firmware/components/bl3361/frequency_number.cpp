#include "frequency_number.h"

#include <cmath>

#include "esphome/core/log.h"

#include "bl3361.h"

namespace esphome::bl3361 {

static const char *const TAG = "bl3361.number";

void FrequencyNumber::show(uint32_t frequency_khz) { this->publish_state(frequency_khz / 1000.0f); }

void FrequencyNumber::control(float mhz) {
  const uint32_t khz = static_cast<uint32_t>(std::lround(mhz * 1000.0f));
  if (!this->parent_->set_frequency(khz))
    ESP_LOGW(TAG, "%.3f MHz is not a frequency the module can tune to (305-340 MHz and 430-440 MHz)", mhz);
  // Show what the radio really uses, which is the old value after a refusal.
  this->show(this->parent_->frequency_khz());
}

}  // namespace esphome::bl3361
