#include "radio_frequency.h"

#if __has_include("esphome/components/ir_rf_base/ir_rf_base.h")
#ifdef USE_RADIO_FREQUENCY

#include <cinttypes>
#include <cstdlib>
#include <vector>

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/log.h"

#include "bl3361.h"

namespace esphome::bl3361 {

static const char *const TAG = "bl3361.radio_frequency";

// Same limit ESPHome applies to codes that arrive base64 encoded.
static constexpr int32_t MAX_TIMING_US = 500000;
// Pause between repeats of a code that does not end with one itself.
static constexpr uint32_t DEFAULT_REPEAT_GAP_US = 10000;

void RadioFrequencyEntity::setup() {
  radio_frequency::RadioFrequency::setup();
  this->set_supports_transmitter(true);
  // The tuning range of the module, in Hz. The two bands it covers are
  // 305-340 MHz and 430-440 MHz; a frequency in between is refused when sending.
  this->traits_.set_frequency_min_hz(BAND_LOW_FIRST_KHZ * 1000u);
  this->traits_.set_frequency_max_hz((BAND_HIGH_LAST_KHZ + 1) * 1000u);
  this->traits_.add_supported_modulation(radio_frequency::RadioFrequencyModulation::RADIO_FREQUENCY_MODULATION_OOK);
}

void RadioFrequencyEntity::dump_config() {
  radio_frequency::RadioFrequency::dump_config();
  ESP_LOGCONFIG(TAG, "  Sends through the BL3361 module, on its own frequency unless the request names one");
}

bool RadioFrequencyEntity::control(const radio_frequency::RadioFrequencyCall &call) {
  // Decode in whichever of the three forms the caller used.
  remote_base::RemoteTransmitData data;
  if (call.is_packed()) {
    data.set_data_from_packed_sint32(call.get_packed_data(), call.get_packed_length(), call.get_packed_count());
  } else if (call.is_base64url()) {
    if (!data.set_data_from_base64url(call.get_base64url_data())) {
      ESP_LOGE(TAG, "Invalid base64url data");
      return false;
    }
  } else if (call.has_raw_timings()) {
    data.set_data(call.get_raw_timings());
  } else {
    ESP_LOGE(TAG, "No raw timings provided");
    return false;
  }

  const std::vector<int32_t> &timings = data.get_data();
  if (timings.empty()) {
    ESP_LOGE(TAG, "No raw timings provided");
    return false;
  }
  for (int32_t timing : timings) {
    if (std::abs(timing) > MAX_TIMING_US) {
      ESP_LOGE(TAG, "Invalid timing value: %" PRId32 " us (max %" PRId32 ")", timing, MAX_TIMING_US);
      return false;
    }
  }

  const uint32_t frequency_khz = call.get_frequency().has_value() ? *call.get_frequency() / 1000 : 0;
  const uint32_t gap_us = timings.back() < 0 ? 0 : DEFAULT_REPEAT_GAP_US;
  const bool sent = this->parent_->send_raw(timings, call.get_repeat_count(), gap_us, frequency_khz);

#if defined(USE_API) && defined(USE_IR_RF_TRANSMIT_COMPLETE)
  // The frame has left the module when send_raw() returns. With completion
  // tracking in the build, the API request is only answered by this call.
  if (call.wants_api_reply())
    this->api_transmit_done_(sent);
#endif
  return sent;
}

}  // namespace esphome::bl3361

#endif  // USE_RADIO_FREQUENCY
#endif  // __has_include
