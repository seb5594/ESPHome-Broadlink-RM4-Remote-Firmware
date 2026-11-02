#pragma once

// Turns the frames of the framer into what a user wants to hear about: one
// event per transmission of a remote.
//
// A key that is held (or pressed once, many remotes repeat their code several
// times) keeps the line busy with short pauses in between. The framer only
// ends a frame at a pause longer than the idle time or when its pulse limit is
// reached, so one press arrives as several frames that each hold a piece of the
// signal: a long preamble without any pause, then the code, its repeats, and
// the same again in the next frame. This filter
//   - cuts every frame into the single transmissions at the pauses
//     (split_burst() in frame_match.h),
//   - drops the piece in front of the first pause of a frame that continues
//     the one before, because it starts in the middle of a signal,
//   - drops what comes after the last pause of a frame, which is no complete
//     transmission,
//   - and, if wanted, swallows a transmission that is the same as the one just
//     before it, so a repeating key gives one event instead of a dozen.
//
// Learning does not use it, that has its own logic for the repeats.
//
// Like protocol.h it has no dependency on ESPHome and is tested on a PC.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "frame_match.h"

namespace esphome::bl3361 {

class ReceiveFilter {
 public:
  /// `frame_limit` is the pulse limit of the framer: a frame that reaches it
  /// was cut off, not ended by silence. `repeat_timeout_ms` is how long the
  /// same transmission is taken as a repeat, 0 passes every transmission.
  ReceiveFilter(size_t frame_limit, size_t min_pulses, uint32_t repeat_timeout_ms, uint32_t min_gap_us = 4000)
      : frame_limit_(frame_limit),
        min_pulses_(min_pulses),
        repeat_timeout_ms_(repeat_timeout_ms),
        min_gap_us_(min_gap_us) {}

  /// Feeds one frame, `now_ms` is the time it arrived. Returns the
  /// transmissions to publish, oldest first.
  std::vector<std::vector<int32_t>> push(const std::vector<int32_t> &frame, uint32_t now_ms) {
    std::vector<std::vector<int32_t>> pieces = split_burst(frame, this->min_gap_us_);
    if (this->continues_ && !pieces.empty())
      pieces.erase(pieces.begin());
    this->continues_ = frame.size() >= this->frame_limit_;

    std::vector<std::vector<int32_t>> out;
    for (auto &piece : pieces) {
      if (piece.size() < this->min_pulses_)
        continue;
      if (this->is_repeat_(piece, now_ms)) {
        this->last_ms_ = now_ms;  // a key that is held keeps the window open
        continue;
      }
      this->last_ = piece;
      this->last_ms_ = now_ms;
      this->has_last_ = true;
      out.push_back(std::move(piece));
    }
    return out;
  }

  /// Forgets what came before, for example after the radio was retuned.
  void reset() {
    this->continues_ = false;
    this->has_last_ = false;
    this->last_.clear();
  }

 protected:
  bool is_repeat_(const std::vector<int32_t> &piece, uint32_t now_ms) const {
    if (this->repeat_timeout_ms_ == 0 || !this->has_last_)
      return false;
    if (static_cast<uint32_t>(now_ms - this->last_ms_) > this->repeat_timeout_ms_)
      return false;
    return frames_match(this->last_, piece);
  }

  size_t frame_limit_;
  size_t min_pulses_;
  uint32_t repeat_timeout_ms_;
  uint32_t min_gap_us_;

  bool continues_{false};
  bool has_last_{false};
  std::vector<int32_t> last_;
  uint32_t last_ms_{0};
};

}  // namespace esphome::bl3361
