#pragma once

#include <array>
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome {
namespace rtl87xx_mac {

class Rtl87xxMac : public Component {
 public:
  void set_mac_address(const std::array<uint8_t, 6> &mac) { this->mac_ = mac; }

  void setup() override;
  void dump_config() override;
  // Has to run before the Wi-Fi component starts the radio.
  float get_setup_priority() const override { return setup_priority::WIFI + 1.0f; }

 protected:
  std::array<uint8_t, 6> mac_{};
  // The driver keeps a pointer to this text, so it must live as long as the program.
  char mac_text_[18]{};
};

}  // namespace rtl87xx_mac
}  // namespace esphome
