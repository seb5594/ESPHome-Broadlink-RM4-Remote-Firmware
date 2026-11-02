#pragma once

#include <cstdint>

namespace esphome::bl3361 {

/// A time window in which received pulses are thrown away.
///
/// The receiver of the module has no squelch, and right after it is tuned it
/// produces a burst of random pulses that look like frames. Everything that
/// arrives while the gate is closed is noise from the tuning and not a signal.
class SettleGate {
 public:
  /// Closes the gate until `duration_ms` have passed since `now_ms`.
  void close_for(uint32_t now_ms, uint32_t duration_ms) {
    this->until_ms_ = now_ms + duration_ms;
    this->closed_ = duration_ms != 0;
  }

  bool is_closed(uint32_t now_ms) {
    if (this->closed_ && static_cast<int32_t>(now_ms - this->until_ms_) >= 0)
      this->closed_ = false;  // also correct when the millisecond counter wrapped
    return this->closed_;
  }

 protected:
  uint32_t until_ms_{0};
  bool closed_{false};
};

}  // namespace esphome::bl3361
