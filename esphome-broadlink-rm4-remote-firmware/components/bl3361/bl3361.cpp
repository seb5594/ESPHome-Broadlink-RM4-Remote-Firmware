#include "bl3361.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <string>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "frequency_number.h"
#include "frequency_select.h"

namespace esphome::bl3361 {

static const char *const TAG = "bl3361";

// ---------------------------------------------------------------------------
// Component life cycle
// ---------------------------------------------------------------------------

void BL3361Component::setup() {
  if (this->has_frequency_entity_() && this->restore_frequency_) {
    this->frequency_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("bl3361_frequency"));
    uint32_t saved_khz = 0;
    FrequencyPlan probe{};
    if (this->frequency_pref_.load(&saved_khz) && plan_frequency(saved_khz, &probe)) {
      ESP_LOGI(TAG, "Restored the frequency of the last session: %.3f MHz", saved_khz / 1e3f);
      this->frequency_khz_ = saved_khz;
    }
  }
  if (!plan_frequency(this->frequency_khz_, &this->plan_)) {
    ESP_LOGE(TAG, "%.3f MHz is outside the 305-340 MHz and 430-440 MHz the module can tune to",
             this->frequency_khz_ / 1e3f);
    this->mark_failed();
    return;
  }
  this->serial_.open();
  this->data_pin_->setup();
  this->isr_pin_ = this->data_pin_->to_isr();
  // Park the pin as an input. The edge interrupt is only attached once the
  // module is switched to receive.
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);
  this->show_frequency_();
}

void BL3361Component::dump_config() {
  ESP_LOGCONFIG(TAG,
                "BL3361 RF module (EXPERIMENTAL):\n"
                "  Frequency: %.3f MHz (base %" PRIu32 " kHz, step %" PRIu32 " kHz, channel %u of %u)\n"
                "  Mode: %s\n"
                "  Frames are ignored for %" PRIu32 " ms after tuning",
                this->plan_.frequency_khz() / 1e3f, this->plan_.base_khz, this->plan_.step_khz, this->plan_.channel,
                this->plan_.channels,
                this->mode_ == Mode::RX ? "receiving" : (this->mode_ == Mode::TX ? "transmitting" : "not started"),
                this->settle_ms_);
  LOG_PIN("  Data pin: ", this->data_pin_);
  if (this->has_frequency_entity_())
    ESP_LOGCONFIG(TAG, "  Frequency entities: select %s, number %s, last frequency kept %s",
                  YESNO(this->frequency_select_ != nullptr), YESNO(this->frequency_number_ != nullptr),
                  YESNO(this->restore_frequency_));
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "RSSI", this->rssi_sensor_);
#endif
}

void BL3361Component::loop() {
  const uint32_t now = millis();

  if (this->task_ == Task::FIND || this->task_ == Task::LEARN_SWEEP) {
    this->run_sweep_(now);
    return;
  }

  if (this->mode_ == Mode::UNKNOWN) {
    // Not listening yet: either the first attempt after boot or a retry after
    // a failure (no module on the UART, for example on a device without RF).
    const bool due = !this->attempted_ ? now >= 500 : now - this->last_attempt_ms_ >= RETRY_INTERVAL_MS;
    if (this->wanted_rx_ && due) {
      this->attempted_ = true;
      this->last_attempt_ms_ = now;
      this->enter_rx_();
    }
    return;
  }

  if (this->mode_ != Mode::RX)
    return;

  this->process_edges_();

  if (this->task_ == Task::LEARN_CAPTURE && static_cast<int32_t>(now - this->capture_deadline_ms_) >= 0) {
    this->task_ = Task::NONE;
    ESP_LOGW(TAG, "No signal that repeats within %u s. Press the key several times, closer to the module.",
             (unsigned) (LEARN_CAPTURE_TIMEOUT_MS / 1000));
    this->fire_learn_("timeout", this->plan_.frequency_khz(), 0, {});
  }

#ifdef USE_SENSOR
  if (this->rssi_sensor_ != nullptr && now - this->last_rssi_ms_ >= this->rssi_interval_ms_) {
    this->last_rssi_ms_ = now;
    uint8_t rssi = 0;
    if (this->read_rssi_(&rssi))
      this->rssi_sensor_->publish_state(rssi);
  }
#endif
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool BL3361Component::start_receive() {
  if (this->task_ != Task::NONE) {
    ESP_LOGW(TAG, "Busy with a frequency search or learning, stop it first");
    return false;
  }
  this->wanted_rx_ = true;
  return this->enter_rx_();
}

void BL3361Component::stop() {
  this->task_ = Task::NONE;
  this->sweep_.cancel();
  this->wanted_rx_ = false;
  this->data_pin_->detach_interrupt();
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);
  this->transact_(COMMAND_IDLE, nullptr, 0);
  this->mode_ = Mode::UNKNOWN;
}

bool BL3361Component::set_frequency(uint32_t frequency_khz) {
  FrequencyPlan plan{};
  if (!plan_frequency(frequency_khz, &plan)) {
    ESP_LOGW(TAG, "%" PRIu32 " kHz is outside the supported bands, keeping %.3f MHz", frequency_khz,
             this->plan_.frequency_khz() / 1e3f);
    return false;
  }
  if (this->task_ != Task::NONE) {
    ESP_LOGW(TAG, "Busy with a frequency search or learning, keeping %.3f MHz", this->plan_.frequency_khz() / 1e3f);
    return false;
  }
  this->plan_ = plan;
  this->frequency_khz_ = frequency_khz;
  ESP_LOGI(TAG, "Frequency set to %.3f MHz", plan.frequency_khz() / 1e3f);
  this->frequency_changed_(true);
  return this->mode_ == Mode::RX ? this->enter_rx_() : true;
}

void BL3361Component::frequency_changed_(bool persist) {
  if (persist && this->has_frequency_entity_() && this->restore_frequency_) {
    uint32_t khz = this->plan_.frequency_khz();
    this->frequency_pref_.save(&khz);
  }
  this->show_frequency_();
}

void BL3361Component::show_frequency_() {
  if (this->frequency_select_ != nullptr)
    this->frequency_select_->show(this->plan_.frequency_khz());
  if (this->frequency_number_ != nullptr)
    this->frequency_number_->show(this->plan_.frequency_khz());
}

bool BL3361Component::send_raw(const std::vector<int32_t> &code, uint32_t repeat, uint32_t gap_us,
                               uint32_t frequency_khz) {
  if (this->task_ != Task::NONE) {
    ESP_LOGW(TAG, "Busy with a frequency search or learning, not sending");
    return false;
  }
  if (code.empty() || code.size() > MAX_SEND_PULSES) {
    ESP_LOGW(TAG, "Refusing to send %u pulses (allowed: 1 to %u)", (unsigned) code.size(),
             (unsigned) MAX_SEND_PULSES);
    return false;
  }
  repeat = std::min<uint32_t>(std::max<uint32_t>(repeat, 1), MAX_SEND_REPEAT);

  // Another frequency is only used for this transmission, the radio goes back
  // to the configured one afterwards.
  FrequencyPlan tx_plan = this->plan_;
  if (frequency_khz != 0 && frequency_khz != this->plan_.frequency_khz() && !plan_frequency(frequency_khz, &tx_plan)) {
    ESP_LOGW(TAG, "%" PRIu32 " kHz is outside the supported bands, not sending", frequency_khz);
    return false;
  }

  if (!this->enter_tx_(tx_plan)) {
    this->mode_ = Mode::UNKNOWN;  // loop() brings the receiver back
    return false;
  }
  ESP_LOGD(TAG, "Sending %u pulses, %" PRIu32 " times, at %.3f MHz", (unsigned) code.size(), repeat,
           tx_plan.frequency_khz() / 1e3f);

  for (uint32_t i = 0; i < repeat; i++) {
    for (int32_t pulse : code) {
      this->data_pin_->digital_write(pulse > 0);
      delay_microseconds_safe(static_cast<uint32_t>(pulse > 0 ? pulse : -pulse));
    }
    this->data_pin_->digital_write(false);
    if (gap_us != 0 && i + 1 < repeat)
      delay_microseconds_safe(gap_us);
    App.feed_wdt();
  }
  this->data_pin_->digital_write(false);

  this->mode_ = Mode::UNKNOWN;
  if (this->wanted_rx_)
    this->enter_rx_();
  return true;
}

void BL3361Component::scan() {
  if (this->task_ != Task::NONE) {
    ESP_LOGW(TAG, "Busy with a frequency search or learning, not scanning");
    return;
  }
  if (this->mode_ != Mode::RX && !this->start_receive())
    return;

  ESP_LOGI(TAG, "Signal strength around %.3f MHz (relative, higher is stronger):", this->plan_.frequency_khz() / 1e3f);
  uint8_t strongest = 0;
  uint8_t strongest_channel = 0;
  for (uint8_t channel = 0; channel < this->plan_.channels; channel++) {
    const uint8_t request[2] = {channel, DIRECTION_RX};
    if (!this->transact_(COMMAND_SET_CHANNEL, request, sizeof(request)))
      break;
    delay(5);
    // The reading jitters, the highest of a few samples is more telling.
    uint8_t level = 0;
    for (int sample = 0; sample < 3; sample++) {
      uint8_t rssi = 0;
      if (this->read_rssi_(&rssi))
        level = std::max(level, rssi);
    }
    if (level > strongest) {
      strongest = level;
      strongest_channel = channel;
    }
    ESP_LOGI(TAG, "  %.3f MHz  %3u %s%s", this->plan_.channel_khz(channel) / 1e3f, level,
             std::string(std::min<size_t>(level / 4, 40), '#').c_str(), channel == this->plan_.channel ? "  <- tuned" : "");
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "Strongest: %.3f MHz (%u)", this->plan_.channel_khz(strongest_channel) / 1e3f, strongest);

  // Back to the configured channel, and forget what the pin did meanwhile.
  const uint8_t restore[2] = {this->plan_.channel, DIRECTION_RX};
  this->transact_(COMMAND_SET_CHANNEL, restore, sizeof(restore));
  this->edges_.clear();
  this->framer_.reset();
}

// ---------------------------------------------------------------------------
// Frequency search and learning
// ---------------------------------------------------------------------------

bool BL3361Component::find_frequency(bool apply) {
  if (this->task_ != Task::NONE) {
    ESP_LOGW(TAG, "Already busy with a frequency search or learning");
    return false;
  }
  if (!this->module_seen_ && !this->transact_(COMMAND_IDLE, nullptr, 0)) {
    this->fire_learn_("failed", this->plan_.frequency_khz(), 0, {});
    return false;
  }
  this->module_seen_ = true;

  this->data_pin_->detach_interrupt();
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);
  this->mode_ = Mode::UNKNOWN;
  this->task_ = Task::FIND;
  this->sweep_apply_ = apply;
  this->last_sweep_log_passes_ = 0;
  this->sweep_.start(millis());
  ESP_LOGI(TAG, "Searching for a transmitter. Hold a key of the remote close to the module.");
  this->fire_learn_("sweep", this->plan_.frequency_khz(), 0, {});
  return true;
}

void BL3361Component::cancel() {
  if (this->task_ == Task::NONE)
    return;
  this->task_ = Task::NONE;
  this->sweep_.cancel();
  ESP_LOGI(TAG, "Cancelled");
  this->fire_learn_("cancelled", this->plan_.frequency_khz(), 0, {});
  this->mode_ = Mode::UNKNOWN;
  if (this->wanted_rx_)
    this->enter_rx_();
}

bool BL3361Component::learn() {
  if (!this->find_frequency(true))
    return false;
  this->task_ = Task::LEARN_SWEEP;
  return true;
}

void BL3361Component::run_sweep_(uint32_t now) {
  struct Radio {
    BL3361Component *self;
    bool tune(const FrequencyPlan &plan) { return self->sweep_tune_(plan); }
    bool read_rssi(uint8_t channel, uint8_t *rssi) { return self->sweep_read_(channel, rssi); }
  } radio{this};

  this->sweep_.step(radio, now);

  if (this->sweep_.passes() != this->last_sweep_log_passes_) {
    this->last_sweep_log_passes_ = this->sweep_.passes();
    ESP_LOGD(TAG, "Pass %u: strongest %.3f MHz at %u, average %u", (unsigned) this->sweep_.passes(),
             this->sweep_.last_peak_khz() / 1e3f, (unsigned) this->sweep_.last_peak_rssi(),
             (unsigned) this->sweep_.last_average());
  }
  if (!this->sweep_.running())
    this->finish_sweep_(now);
}

void BL3361Component::finish_sweep_(uint32_t now) {
  const Task task = this->task_;
  this->task_ = Task::NONE;

  switch (this->sweep_.state()) {
    case SweepState::FOUND: {
      const uint32_t khz = this->sweep_.result_khz();
      ESP_LOGI(TAG, "Found a transmitter on %.3f MHz (signal strength %u)", khz / 1e3f,
               (unsigned) this->sweep_.result_rssi());
      this->fire_learn_("found", khz, 0, {});
      FrequencyPlan plan{};
      if ((this->sweep_apply_ || task == Task::LEARN_SWEEP) && plan_frequency(khz, &plan)) {
        this->plan_ = plan;
        this->frequency_khz_ = khz;
        this->frequency_changed_(true);
      }
      if (task == Task::LEARN_SWEEP) {
        this->begin_capture_(now);
        return;
      }
      break;
    }
    case SweepState::TIMEOUT:
      ESP_LOGW(TAG,
               "No transmitter found within %u s. Hold the key longer and closer to the module; "
               "a weak remote may need a lower sweep_margin.",
               (unsigned) (FrequencySweep::TIMEOUT_MS / 1000));
      this->fire_learn_("timeout", this->plan_.frequency_khz(), 0, {});
      break;
    default:
      ESP_LOGW(TAG, "The frequency search stopped, the module did not answer");
      this->fire_learn_("failed", this->plan_.frequency_khz(), 0, {});
      break;
  }

  // Back to listening, on the same frequency unless the search changed it.
  this->mode_ = Mode::UNKNOWN;
  if (this->wanted_rx_)
    this->enter_rx_();
}

void BL3361Component::begin_capture_(uint32_t now) {
  this->wanted_rx_ = true;
  if (!this->enter_rx_()) {
    this->fire_learn_("failed", this->plan_.frequency_khz(), 0, {});
    return;
  }
  this->collector_.reset();
  this->task_ = Task::LEARN_CAPTURE;
  this->capture_deadline_ms_ = now + LEARN_CAPTURE_TIMEOUT_MS;
  ESP_LOGI(TAG, "Now press the key briefly, a few times, on %.3f MHz", this->plan_.frequency_khz() / 1e3f);
  this->fire_learn_("capture", this->plan_.frequency_khz(), 0, {});
}

void BL3361Component::handle_learn_frame_(const std::vector<int32_t> &frame) {
  // A held key repeats its code within one frame, so look at the pieces.
  if (!this->collector_.push_burst(frame))
    return;
  this->task_ = Task::NONE;
  ESP_LOGI(TAG, "Learned a code of %u pulses on %.3f MHz, seen %u times", (unsigned) this->collector_.code().size(),
           this->plan_.frequency_khz() / 1e3f, (unsigned) this->collector_.repeats());
  this->fire_learn_("learned", this->plan_.frequency_khz(), this->collector_.repeats(), this->collector_.code());
}

void BL3361Component::fire_learn_(const char *phase, uint32_t frequency_khz, uint32_t repeats,
                                  const std::vector<int32_t> &code) {
  this->learn_trigger_.trigger(std::string(phase), frequency_khz / 1e3f, repeats, code);
}

bool BL3361Component::sweep_tune_(const FrequencyPlan &plan) {
  if (!this->configure_(plan, DIRECTION_RX))
    return false;
  delay(MODE_SETTLE_MS);
  return true;
}

bool BL3361Component::sweep_read_(uint8_t channel, uint8_t *rssi) {
  const uint8_t request[2] = {channel, DIRECTION_RX};
  if (!this->transact_(COMMAND_SET_CHANNEL, request, sizeof(request)))
    return false;
  delay(3);
  // The reading jitters, the higher of two samples is more telling.
  bool any = false;
  uint8_t level = 0;
  for (int sample = 0; sample < 2; sample++) {
    uint8_t reading = 0;
    if (this->read_rssi_(&reading)) {
      level = std::max(level, reading);
      any = true;
    }
  }
  *rssi = level;
  return any;
}

// ---------------------------------------------------------------------------
// UART side
// ---------------------------------------------------------------------------

bool BL3361Component::transact_(uint16_t command, const uint8_t *payload, size_t payload_size, uint8_t *reply,
                                size_t *reply_size) {
  uint8_t frame[FRAME_MAX_SIZE];
  const size_t size = encode_frame(command, payload, payload_size, frame, sizeof(frame));
  if (size == 0)
    return false;

  // Forget whatever the module said before, such as late replies.
  uint8_t byte;
  while (this->serial_.available() > 0 && this->serial_.read_byte(&byte)) {
  }
  this->decoder_.reset();

  ESP_LOGV(TAG, "TX 0x%03X: %s", (unsigned) command, format_hex_pretty(frame, size).c_str());
  this->serial_.write_array(frame, size);
  this->serial_.flush();

  // What arrived, kept for the log if no valid reply follows.
  uint8_t received[16];
  size_t received_size = 0;
  size_t received_total = 0;

  const uint32_t started = millis();
  while (millis() - started < REPLY_TIMEOUT_MS) {
    while (this->serial_.available() > 0 && this->serial_.read_byte(&byte)) {
      if (received_size < sizeof(received))
        received[received_size++] = byte;
      received_total++;
      if (!this->decoder_.push(byte))
        continue;
      if (this->decoder_.command() != reply_id(command)) {
        ESP_LOGW(TAG, "Unexpected reply 0x%03X to command 0x%03X", (unsigned) this->decoder_.command(),
                 (unsigned) command);
        this->decoder_.reset();
        continue;
      }
      ESP_LOGV(TAG, "RX 0x%03X, %u payload bytes", (unsigned) this->decoder_.command(),
               (unsigned) this->decoder_.payload_size());
      if (reply_size != nullptr && reply != nullptr) {
        *reply_size = this->decoder_.payload_size();
        std::copy_n(this->decoder_.payload(), *reply_size, reply);
      }
      return true;
    }
    App.feed_wdt();
    delay(1);
  }
  this->log_missing_reply_(command, received, received_size, received_total);
  return false;
}

void BL3361Component::log_missing_reply_(uint16_t command, const uint8_t *received, size_t received_size,
                                         size_t received_total) {
  if (received_total == 0) {
    ESP_LOGW(TAG, "No reply to command 0x%03X within %u ms, nothing arrived on the UART", (unsigned) command,
             (unsigned) REPLY_TIMEOUT_MS);
    return;
  }
  // Bytes but no frame: wrong format or speed, or a line that picks up noise.
  ESP_LOGW(TAG, "No valid reply to command 0x%03X within %u ms, %u bytes arrived on the UART: %s%s", (unsigned) command,
           (unsigned) REPLY_TIMEOUT_MS, (unsigned) received_total, format_hex_pretty(received, received_size).c_str(),
           received_total > received_size ? " ..." : "");
}

bool BL3361Component::configure_(const FrequencyPlan &plan, Direction direction) {
  uint8_t blob[CONFIG_BLOB_SIZE];
  size_t blob_size = 0;
  if (!build_config(plan, blob, sizeof(blob), &blob_size)) {
    ESP_LOGE(TAG, "No configuration for base frequency %" PRIu32 " kHz", plan.base_khz);
    return false;
  }
  // The same three steps the stock firmware runs for every mode change:
  // load the tuning, switch the direction, pick the channel.
  const uint16_t switch_command = direction == DIRECTION_RX ? COMMAND_ENTER_RX : COMMAND_ENTER_TX;
  const uint8_t channel[2] = {plan.channel, direction};
  return this->transact_(COMMAND_CONFIGURE, blob, blob_size) && this->transact_(switch_command, nullptr, 0) &&
         this->transact_(COMMAND_SET_CHANNEL, channel, sizeof(channel));
}

bool BL3361Component::read_rssi_(uint8_t *rssi) {
  uint8_t reply[4];
  size_t reply_size = 0;
  if (!this->transact_(COMMAND_GET_RSSI, nullptr, 0, reply, &reply_size) || reply_size < 1)
    return false;
  *rssi = reply[0];
  return true;
}

// ---------------------------------------------------------------------------
// Data pin side
// ---------------------------------------------------------------------------

bool BL3361Component::enter_rx_() {
  this->data_pin_->detach_interrupt();
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);
  this->mode_ = Mode::UNKNOWN;

  // The stock firmware sends the module to idle once at start. It doubles as
  // a cheap check that anybody listens on the UART before the longer sequence.
  if (!this->module_seen_) {
    if (!this->transact_(COMMAND_IDLE, nullptr, 0)) {
      ESP_LOGW(TAG, "The RF module does not answer on the UART. Is this a Broadlink RM4 Pro? Retrying every %u s.",
               (unsigned) (RETRY_INTERVAL_MS / 1000));
      return false;
    }
    this->module_seen_ = true;
  }
  if (!this->configure_(this->plan_, DIRECTION_RX))
    return false;

  delay(MODE_SETTLE_MS);
  this->edges_.clear();
  this->framer_.reset();
  this->last_edge_us_ = micros();
  // The receiver is noisy right after tuning, see SettleGate.
  this->settle_gate_.close_for(millis(), this->settle_ms_);
  this->data_pin_->attach_interrupt(&BL3361Component::on_edge_, this, gpio::INTERRUPT_ANY_EDGE);
  this->mode_ = Mode::RX;
  ESP_LOGI(TAG, "Listening on %.3f MHz", this->plan_.frequency_khz() / 1e3f);
  return true;
}

bool BL3361Component::enter_tx_(const FrequencyPlan &plan) {
  this->data_pin_->detach_interrupt();
  this->data_pin_->pin_mode(gpio::FLAG_OUTPUT);
  this->data_pin_->digital_write(false);
  this->mode_ = Mode::UNKNOWN;

  if (!this->configure_(plan, DIRECTION_TX)) {
    this->data_pin_->pin_mode(gpio::FLAG_INPUT);
    return false;
  }
  delay(MODE_SETTLE_MS);
  this->mode_ = Mode::TX;
  return true;
}

void IRAM_ATTR HOT BL3361Component::on_edge_(BL3361Component *self) {
  // Only note how long the line stayed at the level it just left. The level
  // read now belongs to the new pulse.
  const bool level_now = self->isr_pin_.digital_read();
  const uint32_t now = micros();
  self->edges_.push(elapsed_us(now, self->last_edge_us_), !level_now);
  self->last_edge_us_ = now;
}

void BL3361Component::process_edges_() {
  if (this->settle_gate_.is_closed(millis())) {
    // Noise of the tuning, not a signal.
    uint32_t duration_us;
    bool level;
    while (this->edges_.pop(&duration_us, &level)) {
    }
    this->framer_.reset();
    return;
  }

  uint32_t duration_us;
  bool level;
  while (this->edges_.pop(&duration_us, &level)) {
    if (this->framer_.push(duration_us, level))
      this->publish_frame_(this->framer_.frame());
  }
  // A frame ends with a pause, which no edge announces. Look at the clock.
  if (this->framer_.has_pending() && elapsed_us(micros(), this->last_edge_us_) > this->idle_us_ &&
      this->framer_.flush())
    this->publish_frame_(this->framer_.frame());

  const uint32_t overflows = this->edges_.overflows();
  if (overflows != this->reported_overflows_) {
    ESP_LOGW(TAG, "Pulses were lost, the main loop is too slow for this signal (%" PRIu32 " so far)", overflows);
    this->reported_overflows_ = overflows;
  }
}

void BL3361Component::publish_frame_(const std::vector<int32_t> &frame) {
  if (this->log_frames_) {
    // Keep the log line within reason; the trigger always gets the whole frame.
    constexpr size_t MAX_LOGGED = 100;
    std::string text;
    char piece[16];
    for (size_t i = 0; i < frame.size() && i < MAX_LOGGED; i++) {
      std::snprintf(piece, sizeof(piece), i == 0 ? "%+d" : ", %+d", (int) frame[i]);
      text += piece;
    }
    if (frame.size() > MAX_LOGGED)
      text += ", ...";
    ESP_LOGI(TAG, "Received %u pulses: [%s]", (unsigned) frame.size(), text.c_str());
  }
  if (this->task_ == Task::LEARN_CAPTURE) {
    this->handle_learn_frame_(frame);  // a learning session keeps the frames to itself
    return;
  }
  this->receive_trigger_.trigger(frame);
}

}  // namespace esphome::bl3361
