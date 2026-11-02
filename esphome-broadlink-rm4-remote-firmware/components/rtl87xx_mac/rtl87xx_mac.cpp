#include "rtl87xx_mac.h"

#ifdef USE_LIBRETINY

#include <cstdio>

#include "esphome/core/log.h"

// Global of the closed-source Realtek driver. When it points to an
// "aa:bb:cc:dd:ee:ff" string, the driver uses that as the station MAC instead of
// the eFuse value. It is read every time the radio starts, so setting it once
// before Wi-Fi is enough.
//
// Not to be confused with wifi_set_mac_address() / WiFi.setMacAddress() of older
// LibreTiny releases: those program the one-time-programmable eFuse.
extern "C" char *rtw_initmac;

namespace esphome {
namespace rtl87xx_mac {

static const char *const TAG = "rtl87xx_mac";

void Rtl87xxMac::setup() {
  snprintf(this->mac_text_, sizeof(this->mac_text_), "%02x:%02x:%02x:%02x:%02x:%02x", this->mac_[0], this->mac_[1],
           this->mac_[2], this->mac_[3], this->mac_[4], this->mac_[5]);
  rtw_initmac = this->mac_text_;
}

void Rtl87xxMac::dump_config() { ESP_LOGCONFIG(TAG, "RTL87xx MAC address: %s (RAM only)", this->mac_text_); }

}  // namespace rtl87xx_mac
}  // namespace esphome

#endif  // USE_LIBRETINY
