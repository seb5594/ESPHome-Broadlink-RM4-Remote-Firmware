#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include "bl3361.h"

namespace esphome::bl3361 {

template<typename... Ts> class StartReceiveAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  void play(const Ts &...x) override { this->parent_->start_receive(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

template<typename... Ts> class ScanAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  void play(const Ts &...x) override { this->parent_->scan(); }
};

template<typename... Ts> class CancelAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  void play(const Ts &...x) override { this->parent_->cancel(); }
};

template<typename... Ts> class FindFrequencyAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  TEMPLATABLE_VALUE(bool, apply)

  void play(const Ts &...x) override { this->parent_->find_frequency(this->apply_.value(x...)); }
};

template<typename... Ts> class LearnAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  void play(const Ts &...x) override { this->parent_->learn(); }
};

template<typename... Ts> class SetFrequencyAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  TEMPLATABLE_VALUE(uint32_t, frequency_hz)

  void play(const Ts &...x) override { this->parent_->set_frequency(this->frequency_hz_.value(x...) / 1000); }
};

template<typename... Ts> class SendRawAction : public Action<Ts...>, public Parented<BL3361Component> {
 public:
  TEMPLATABLE_VALUE(uint32_t, repeat)
  TEMPLATABLE_VALUE(uint32_t, gap_us)
  TEMPLATABLE_VALUE(uint32_t, frequency_hz)

  void set_code_template(std::function<std::vector<int32_t>(Ts...)> func) {
    this->code_func_ = func;
    this->has_static_code_ = false;
  }
  void set_code_static(std::vector<int32_t> code) {
    this->code_static_ = std::move(code);
    this->has_static_code_ = true;
  }

  void play(const Ts &...x) override {
    if (this->has_static_code_) {
      this->parent_->send_raw(this->code_static_, this->repeat_.value(x...), this->gap_us_.value(x...),
                              this->frequency_hz_.value(x...) / 1000);
    } else {
      this->parent_->send_raw(this->code_func_(x...), this->repeat_.value(x...), this->gap_us_.value(x...),
                              this->frequency_hz_.value(x...) / 1000);
    }
  }

 protected:
  bool has_static_code_{true};
  std::vector<int32_t> code_static_;
  std::function<std::vector<int32_t>(Ts...)> code_func_;
};

}  // namespace esphome::bl3361
