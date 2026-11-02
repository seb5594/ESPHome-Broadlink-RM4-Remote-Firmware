#pragma once

// Driver for the BL3361 433 MHz module of the Broadlink RM4 Pro.
// EXPERIMENTAL, see protocol.h for where the knowledge comes from.
//
// The module is controlled over a UART (tuning, receive/transmit switch,
// signal strength) while the actual radio signal passes through one raw OOK
// data pin that is shared for both directions:
//
//   receive   the module drives the pin with the demodulated signal, an edge
//             interrupt measures the pulses
//   transmit  the firmware drives the pin and the module keys its transmitter
//             with it; the pulses are timed by software
//
// One component owns everything, so the pin never has two masters. That is
// also why remote_receiver / remote_transmitter are not used here: on the
// RTL8720C an interrupt and a GPIO output cannot be active on the same pad,
// and ESPHome's transmitter drives the pad through the PWM peripheral.
//
// On top of that sit the two things the stock firmware offers when it learns a
// remote: finding the frequency of a transmitter (sweep.h) and telling a real
// signal from noise by its repetition (frame_match.h).

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include "edge_ring.h"
#include "frame_match.h"
#include "module_serial.h"
#include "protocol.h"
#include "pulse_framer.h"
#include "radio_config.h"
#include "settle_gate.h"
#include "sweep.h"

namespace esphome::bl3361 {

class FrequencySelect;
class FrequencyNumber;

class BL3361Component : public Component {
 public:
  BL3361Component() : framer_(100, 15000, 24, MAX_FRAME_PULSES) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  // After the UART and the pins, before anything that wants to use the radio.
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_data_pin(InternalGPIOPin *pin) { this->data_pin_ = pin; }
  void set_frequency_khz(uint32_t frequency_khz) { this->frequency_khz_ = frequency_khz; }
  void set_pulse_limits(uint32_t filter_us, uint32_t idle_us, size_t min_pulses) {
    this->framer_ = PulseFramer(filter_us, idle_us, min_pulses, MAX_FRAME_PULSES);
    this->idle_us_ = idle_us;
  }
  void set_log_frames(bool log_frames) { this->log_frames_ = log_frames; }
  /// Frames in the first moments after tuning are noise and are dropped.
  void set_settle_time(uint32_t settle_ms) { this->settle_ms_ = settle_ms; }
  /// How far the strongest channel has to stand out when searching a frequency.
  void set_sweep_margin(uint8_t margin) { this->sweep_.set_margin(margin); }
  /// Entity that shows and sets the frequency. With restore the last frequency
  /// survives a restart.
  void set_frequency_select(FrequencySelect *select, bool restore) {
    this->frequency_select_ = select;
    this->restore_frequency_ = restore;
  }
  /// Entity that takes any frequency in MHz. Remembers the frequency like the
  /// select does when `restore` is set.
  void set_frequency_number(FrequencyNumber *number, bool restore) {
    this->frequency_number_ = number;
    this->restore_frequency_ = this->restore_frequency_ || restore;
  }
#ifdef USE_SENSOR
  void set_rssi_sensor(sensor::Sensor *sensor) { this->rssi_sensor_ = sensor; }
  void set_rssi_interval(uint32_t interval_ms) {
    this->rssi_interval_ms_ = interval_ms;
    this->rssi_wait_ms_ = interval_ms;
  }
#endif
  Trigger<std::vector<int32_t>> *get_receive_trigger() { return &this->receive_trigger_; }
  /// Fires while a frequency search or a learning session progresses: the
  /// phase ("sweep", "found", "capture", "learned", "timeout" or "failed"),
  /// the frequency in MHz, how often the code was seen and the code itself.
  Trigger<std::string, float, uint32_t, std::vector<int32_t>> *get_learn_trigger() { return &this->learn_trigger_; }

  uint32_t frequency_khz() const { return this->plan_.frequency_khz(); }

  // ----- Actions -----

  /// Tunes to the frequency and starts listening.
  bool start_receive();
  /// Stops listening, ends a search or learning session and puts the module into idle.
  void stop();
  /// Sends a raw code (positive = signal on, negative = signal off, in
  /// microseconds), `repeat` times in a row, then goes back to listening.
  /// With a frequency (kHz) other than 0 it transmits on that one and returns to
  /// the configured frequency afterwards.
  bool send_raw(const std::vector<int32_t> &code, uint32_t repeat, uint32_t gap_us, uint32_t frequency_khz = 0);
  /// Retunes (and keeps listening if it was). Fails outside the supported bands.
  /// A frequency set this way is remembered if the frequency entity restores it.
  bool set_frequency(uint32_t frequency_khz);
  /// Reads the signal strength on every channel around the current frequency
  /// and logs the result. Meant for finding the real carrier of a remote.
  void scan();
  /// Searches both bands for a transmitter while a key is held. With `apply`
  /// the module is tuned to what was found. Returns at once, the result comes
  /// through the learn trigger.
  bool find_frequency(bool apply);
  /// Ends a frequency search or learning session and goes back to listening.
  void cancel();
  /// Like the stock firmware's learning: find the frequency, then capture a
  /// signal that repeats. Returns at once, progress comes through the learn trigger.
  bool learn();

 protected:
  static constexpr size_t MAX_FRAME_PULSES = 512;
  static constexpr size_t MAX_SEND_PULSES = 2048;
  static constexpr uint32_t MAX_SEND_REPEAT = 30;
  static constexpr uint32_t REPLY_TIMEOUT_MS = 300;
  static constexpr uint32_t RETRY_INTERVAL_MS = 10000;
  /// A lost signal strength reading holds the loop for REPLY_TIMEOUT_MS, so it is not asked for again right away.
  static constexpr uint32_t RSSI_BACKOFF_MS = 30000;
  static constexpr uint32_t MODE_SETTLE_MS = 10;
  /// How long learning waits for a signal that repeats.
  static constexpr uint32_t LEARN_CAPTURE_TIMEOUT_MS = 20000;

  enum class Mode : uint8_t { UNKNOWN, RX, TX };
  enum class Task : uint8_t { NONE, FIND, LEARN_SWEEP, LEARN_CAPTURE };

  // ----- UART side -----
  /// Logs what arrived on the UART while waiting for a reply that never came.
  void log_missing_reply_(uint16_t command, const uint8_t *received, size_t received_size, size_t received_total);
  /// Sends one request and waits for its reply. Blocks for the round trip,
  /// which is a few milliseconds when the module answers.
  bool transact_(uint16_t command, const uint8_t *payload, size_t payload_size, uint8_t *reply = nullptr,
                 size_t *reply_size = nullptr);
  bool configure_(const FrequencyPlan &plan, Direction direction);
  /// Like transact_(), but a request without an answer is sent once more.
  bool transact_again_(uint16_t command, const uint8_t *payload, size_t payload_size);
  bool read_rssi_(uint8_t *rssi);

  // ----- Data pin side -----
  bool enter_rx_();
  bool enter_tx_(const FrequencyPlan &plan);
  void process_edges_();
  void publish_frame_(const std::vector<int32_t> &frame);
  static void IRAM_ATTR HOT on_edge_(BL3361Component *self);

  // ----- Frequency -----
  /// Remembers the frequency and tells the entity. `persist` also stores it.
  void frequency_changed_(bool persist);
  void show_frequency_();
  bool has_frequency_entity_() const { return this->frequency_select_ != nullptr || this->frequency_number_ != nullptr; }

  // ----- Search and learning -----
  void run_sweep_(uint32_t now);
  void finish_sweep_(uint32_t now);
  void begin_capture_(uint32_t now);
  void handle_learn_frame_(const std::vector<int32_t> &frame);
  void fire_learn_(const char *phase, uint32_t frequency_khz, uint32_t repeats, const std::vector<int32_t> &code);
  /// What the sweep needs from the module, see sweep.h.
  bool sweep_tune_(const FrequencyPlan &plan);
  bool sweep_read_(uint8_t channel, uint8_t *rssi);

  InternalGPIOPin *data_pin_{nullptr};
  ISRInternalGPIOPin isr_pin_;
  uint32_t frequency_khz_{433920};
  FrequencyPlan plan_{};
  Mode mode_{Mode::UNKNOWN};
  bool wanted_rx_{true};
  bool log_frames_{true};

  ModuleSerial serial_;
  FrameDecoder decoder_;
  EdgeRing<1024> edges_;
  PulseFramer framer_;
  SettleGate settle_gate_;
  uint32_t settle_ms_{1500};
  uint32_t idle_us_{15000};
  volatile uint32_t last_edge_us_{0};
  uint32_t last_attempt_ms_{0};
  bool attempted_{false};
  bool module_seen_{false};
  uint32_t reported_overflows_{0};

  Task task_{Task::NONE};
  FrequencySweep sweep_;
  FrameCollector collector_;
  bool sweep_apply_{true};
  uint32_t capture_deadline_ms_{0};
  uint32_t last_sweep_log_passes_{0};

  FrequencySelect *frequency_select_{nullptr};
  FrequencyNumber *frequency_number_{nullptr};
  bool restore_frequency_{false};
  ESPPreferenceObject frequency_pref_;

  Trigger<std::vector<int32_t>> receive_trigger_;
  Trigger<std::string, float, uint32_t, std::vector<int32_t>> learn_trigger_;
#ifdef USE_SENSOR
  sensor::Sensor *rssi_sensor_{nullptr};
  uint32_t rssi_interval_ms_{1000};
  /// The pause before the next reading: the interval, or longer after a reading was lost.
  uint32_t rssi_wait_ms_{1000};
  uint32_t last_rssi_ms_{0};
#endif
};

}  // namespace esphome::bl3361
