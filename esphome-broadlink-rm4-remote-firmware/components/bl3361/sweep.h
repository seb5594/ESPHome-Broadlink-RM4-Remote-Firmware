#pragma once

#include <cstddef>
#include <cstdint>

#include "radio_config.h"

namespace esphome::bl3361 {

// Finding the frequency of a transmitter, the way the stock firmware does it
// when it learns a 433 MHz remote: the user holds a key while the module
// listens its way through the bands.
//
//   1. Coarse sweep. Both bands are read in 500 kHz steps, 305-340 MHz as one
//      request of 70 channels and 430-440 MHz as one of 20 channels. These are
//      the two requests the stock scan builds (basefreq 305000 and 430000,
//      step 500). The channel that stands out of the average is the candidate.
//   2. Refinement in 100 kHz steps, 20 channels around the candidate. If nothing
//      stands out there, the candidate was noise and the search starts over.
//   3. Only inside 315.000-315.999 MHz and 433.000-433.999 MHz, where the module
//      can do 10 kHz steps, a last refinement in 10 kHz steps.
//
// The class only decides what to tune and read next. Talking to the module is
// done by a `Radio`, so the logic can be tested without hardware:
//
//   bool tune(const FrequencyPlan &plan)                    load the tuning, listen
//   bool read_rssi(uint8_t channel, uint8_t *rssi)          select a channel and read it

enum class SweepState : uint8_t { IDLE, COARSE, REFINE_100, REFINE_10, FOUND, TIMEOUT, FAILED };

class FrequencySweep {
 public:
  /// Gives up after this long without a signal.
  static constexpr uint32_t TIMEOUT_MS = 30000;
  /// Channels read per call of step(), so the main loop is never held up long.
  static constexpr uint8_t CHANNELS_PER_STEP = 3;
  /// A reading the module does not answer is skipped, the sweep goes on. It
  /// only stops when this many in a row were lost: then the module is gone.
  static constexpr uint8_t MAX_LOST_READINGS = 5;
  /// How often the tuning of a window is tried before the sweep gives up.
  static constexpr uint8_t MAX_TUNE_ATTEMPTS = 3;

  /// How far the strongest channel has to stand out of the average of all
  /// channels to count as a signal. The reading is coarse: the quiet band sits
  /// around 20 to 23 and a nearby remote reaches about 26.
  void set_margin(uint8_t margin) { this->margin_ = margin; }

  void start(uint32_t now_ms) {
    this->state_ = SweepState::COARSE;
    this->started_ms_ = now_ms;
    this->passes_ = 0;
    this->failed_while_tuning_ = false;
    this->begin_pass_();
  }
  void cancel() { this->state_ = SweepState::IDLE; }

  bool running() const {
    return this->state_ == SweepState::COARSE || this->state_ == SweepState::REFINE_100 ||
           this->state_ == SweepState::REFINE_10;
  }
  SweepState state() const { return this->state_; }
  uint32_t result_khz() const { return this->result_khz_; }
  uint8_t result_rssi() const { return this->result_rssi_; }

  /// What was going on when the state became FAILED, for the log.
  bool failed_while_tuning() const { return this->failed_while_tuning_; }
  const FrequencyPlan &plan() const { return this->plan_; }
  uint8_t channel() const { return this->channel_; }

  /// Statistics of the last finished coarse pass, for the log.
  uint32_t passes() const { return this->passes_; }
  uint32_t last_peak_khz() const { return this->last_peak_khz_; }
  uint8_t last_peak_rssi() const { return this->last_peak_rssi_; }
  uint8_t last_average() const { return this->last_average_; }

  /// Does a small amount of work. Call it from the main loop until running() is false.
  template<typename Radio> SweepState step(Radio &radio, uint32_t now_ms) {
    if (!this->running())
      return this->state_;
    if (now_ms - this->started_ms_ > TIMEOUT_MS) {
      this->state_ = SweepState::TIMEOUT;
      return this->state_;
    }
    if (!this->tuned_) {
      if (!radio.tune(this->plan_)) {
        if (++this->tune_attempts_ >= MAX_TUNE_ATTEMPTS) {
          this->failed_while_tuning_ = true;
          this->state_ = SweepState::FAILED;
        }
        return this->state_;  // otherwise the next call tries again
      }
      this->tuned_ = true;
      return this->state_;
    }
    for (uint8_t i = 0; i < CHANNELS_PER_STEP && this->channel_ < this->plan_.channels; i++, this->channel_++) {
      uint8_t rssi = 0;
      if (!radio.read_rssi(this->channel_, &rssi)) {
        // One lost answer must not end the search. The channel stays unread.
        if (++this->lost_readings_ >= MAX_LOST_READINGS) {
          this->state_ = SweepState::FAILED;
          return this->state_;
        }
        continue;
      }
      this->lost_readings_ = 0;
      this->take_reading_(this->plan_.channel_khz(this->channel_), rssi);
    }
    if (this->channel_ >= this->plan_.channels)
      this->window_done_();
    return this->state_;
  }

 protected:
  static constexpr FrequencyPlan COARSE_LOW{305000, 500, 70, 0};
  static constexpr FrequencyPlan COARSE_HIGH{430000, 500, 20, 0};

  void begin_pass_() {
    this->segment_ = 0;
    this->begin_window_(COARSE_LOW);
  }

  void begin_window_(const FrequencyPlan &plan) {
    this->plan_ = plan;
    this->tuned_ = false;
    this->tune_attempts_ = 0;
    this->lost_readings_ = 0;
    this->channel_ = 0;
    this->sum_ = 0;
    this->count_ = 0;
    this->peak_rssi_ = 0;
    this->peak_khz_ = 0;
  }

  void take_reading_(uint32_t khz, uint8_t rssi) {
    this->sum_ += rssi;
    this->count_++;
    if (this->count_ == 1 || rssi > this->peak_rssi_) {
      this->peak_rssi_ = rssi;
      this->peak_khz_ = khz;
    }
  }

  // Does the peak of the window stand out by at least `margin` of its average?
  bool stands_out_(uint8_t margin) const {
    return this->count_ != 0 && this->peak_rssi_ * this->count_ >= this->sum_ + margin * this->count_;
  }

  void window_done_() {
    switch (this->state_) {
      case SweepState::COARSE:
        if (this->segment_ == 0) {
          this->segment_ = 1;
          this->low_peak_khz_ = this->peak_khz_;
          this->low_peak_rssi_ = this->peak_rssi_;
          this->low_sum_ = this->sum_;
          this->low_count_ = this->count_;
          this->begin_window_(COARSE_HIGH);
          return;
        }
        this->coarse_pass_done_();
        return;
      case SweepState::REFINE_100:
        this->refine_100_done_();
        return;
      case SweepState::REFINE_10:
        this->result_khz_ = this->peak_khz_;
        this->result_rssi_ = this->peak_rssi_;
        this->state_ = SweepState::FOUND;
        return;
      default:
        return;
    }
  }

  void coarse_pass_done_() {
    // Combine both bands into one pass.
    const uint32_t sum = this->low_sum_ + this->sum_;
    const uint32_t count = this->low_count_ + this->count_;
    uint32_t peak_khz = this->low_peak_khz_;
    uint8_t peak_rssi = this->low_peak_rssi_;
    if (this->peak_rssi_ > peak_rssi) {
      peak_rssi = this->peak_rssi_;
      peak_khz = this->peak_khz_;
    }
    this->passes_++;
    this->last_peak_khz_ = peak_khz;
    this->last_peak_rssi_ = peak_rssi;
    this->last_average_ = count == 0 ? 0 : static_cast<uint8_t>(sum / count);

    if (count != 0 && peak_rssi * count >= sum + this->margin_ * count) {
      this->candidate_khz_ = peak_khz;
      this->candidate_rssi_ = peak_rssi;
      this->state_ = SweepState::REFINE_100;
      this->begin_window_(this->window_100_(peak_khz));
      return;
    }
    this->begin_pass_();  // nothing yet, listen through the bands again
  }

  // 20 channels of 100 kHz that contain the candidate with room on both sides.
  static FrequencyPlan window_100_(uint32_t candidate_khz) {
    const bool high = candidate_khz >= BAND_HIGH_FIRST_KHZ;
    const uint32_t first = high ? BAND_HIGH_FIRST_KHZ : BAND_LOW_FIRST_KHZ;
    const uint32_t last = high ? 439000 : 339000;  // last base of the tuning table in each band
    uint32_t from = candidate_khz > 500 ? candidate_khz - 500 : 0;
    uint32_t base = from - from % 1000;
    base = base < first ? first : (base > last ? last : base);
    return FrequencyPlan{base, 100, 20, 0};
  }

  void refine_100_done_() {
    // A transmitter is still there when the neighbourhood is read again, a burst
    // of noise is not. Without the confirmation the search goes back to the
    // coarse pass instead of settling on a frequency where nothing transmits.
    if (!this->stands_out_(this->margin_)) {
      this->state_ = SweepState::COARSE;
      this->begin_pass_();
      return;
    }
    uint32_t khz = this->peak_khz_;
    uint8_t rssi = this->peak_rssi_;
    FrequencyPlan fine;
    if (plan_frequency(khz, &fine) && fine.step_khz == 10) {
      // 10 kHz steps exist here. Start one channel of 100 kHz below, because the
      // 100 kHz channel reaches 50 kHz to either side.
      const uint32_t first = khz - khz % 1000;
      uint32_t base = khz >= 100 ? khz - 100 : khz;
      base = base < first ? first : (base > first + 900 ? first + 900 : base);
      this->state_ = SweepState::REFINE_10;
      this->candidate_khz_ = khz;
      this->candidate_rssi_ = rssi;
      this->begin_window_(FrequencyPlan{base, 10, 20, 0});
      return;
    }
    this->result_khz_ = khz;
    this->result_rssi_ = rssi;
    this->state_ = SweepState::FOUND;
  }

  SweepState state_{SweepState::IDLE};
  uint32_t started_ms_{0};
  uint32_t passes_{0};

  FrequencyPlan plan_{};
  bool tuned_{false};
  bool failed_while_tuning_{false};
  uint8_t tune_attempts_{0};
  uint8_t lost_readings_{0};
  uint8_t channel_{0};
  uint8_t segment_{0};
  uint8_t margin_{3};

  uint32_t sum_{0};
  uint32_t count_{0};
  uint8_t peak_rssi_{0};
  uint32_t peak_khz_{0};

  uint32_t low_sum_{0};
  uint32_t low_count_{0};
  uint8_t low_peak_rssi_{0};
  uint32_t low_peak_khz_{0};

  uint32_t candidate_khz_{0};
  uint8_t candidate_rssi_{0};
  uint32_t result_khz_{0};
  uint8_t result_rssi_{0};

  uint32_t last_peak_khz_{0};
  uint8_t last_peak_rssi_{0};
  uint8_t last_average_{0};
};

}  // namespace esphome::bl3361
