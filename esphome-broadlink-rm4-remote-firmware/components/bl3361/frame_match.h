#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace esphome::bl3361 {

// Telling a real transmission from noise. A remote repeats the same frame for
// as long as a key is held, noise never repeats. The stock firmware learns a
// code the same way: it collects frames and only trusts one that comes back.

/// Number of pulses of `frame` that belong to the signal. The last pulse is
/// the pause that ended the frame (or the marker for a pause that was longer
/// than the receiver waited), it differs from frame to frame.
inline size_t signal_size(const std::vector<int32_t> &frame) { return frame.empty() ? 0 : frame.size() - 1; }

/// Cuts a received frame into the single transmissions it contains.
///
/// A remote that is held repeats its code with a pause of a few milliseconds in
/// between. That pause is shorter than the time that ends a frame, so one frame
/// can hold the same code many times in a row (up to the limit of the framer,
/// 512 pulses). Every space of at least `min_gap_us` ends a piece, and the space
/// stays the last entry of its piece, like the pause at the end of a frame. What
/// follows the last such space is not a complete transmission and is dropped.
inline std::vector<std::vector<int32_t>> split_burst(const std::vector<int32_t> &frame, uint32_t min_gap_us = 4000) {
  std::vector<std::vector<int32_t>> pieces;
  std::vector<int32_t> current;
  for (int32_t pulse : frame) {
    current.push_back(pulse);
    if (pulse < 0 && static_cast<uint32_t>(-static_cast<int64_t>(pulse)) >= min_gap_us) {
      pieces.push_back(std::move(current));
      current.clear();
    }
  }
  return pieces;
}

/// True if two frames are the same signal: equally many pulses, and every pulse
/// within `tolerance_percent` of its counterpart (but never stricter than
/// `slack_us`, because short pulses jitter by a fixed amount).
inline bool frames_match(const std::vector<int32_t> &a, const std::vector<int32_t> &b, uint32_t tolerance_percent = 25,
                         uint32_t slack_us = 120) {
  const size_t size = signal_size(a);
  if (size == 0 || size != signal_size(b))
    return false;
  for (size_t i = 0; i < size; i++) {
    if ((a[i] > 0) != (b[i] > 0))
      return false;
    const uint32_t x = static_cast<uint32_t>(std::abs(a[i]));
    const uint32_t y = static_cast<uint32_t>(std::abs(b[i]));
    const uint32_t allowed = std::max(slack_us, std::max(x, y) * tolerance_percent / 100);
    if ((x > y ? x - y : y - x) > allowed)
      return false;
  }
  return true;
}

/// Collects the frames of a learning session and reports the first signal that
/// was seen often enough.
class FrameCollector {
 public:
  /// `min_pulses` is the least a signal has to have to count: two noise frames
  /// can easily share a single short pulse, but not a dozen of them.
  explicit FrameCollector(size_t needed = 2, size_t max_frames = 12, size_t min_pulses = 12)
      : needed_(needed), max_frames_(max_frames), min_pulses_(min_pulses) {}

  void reset() {
    this->frames_.clear();
    this->matches_ = 0;
  }

  /// Adds a frame. Returns true once some signal has been seen `needed` times.
  bool push(const std::vector<int32_t> &frame) {
    if (signal_size(frame) < this->min_pulses_)
      return false;
    if (this->frames_.size() >= this->max_frames_)
      this->frames_.erase(this->frames_.begin());  // old noise makes room, repeats stay recent
    this->frames_.push_back(frame);

    const std::vector<int32_t> &newest = this->frames_.back();
    size_t count = 1;
    size_t best = this->frames_.size() - 1;
    for (size_t i = 0; i + 1 < this->frames_.size(); i++) {
      if (frames_match(this->frames_[i], newest)) {
        count++;
        best = i;  // the earliest match is the first transmission
        break;
      }
    }
    if (count < 2)
      return false;
    // Count how often the signal came back in total.
    size_t total = 0;
    for (const auto &candidate : this->frames_) {
      if (frames_match(candidate, newest))
        total++;
    }
    this->matches_ = total;
    this->best_ = best;
    return total >= this->needed_;
  }

  /// Adds a received frame that may hold several transmissions, see split_burst().
  /// Returns true once some signal has been seen `needed` times.
  bool push_burst(const std::vector<int32_t> &frame, uint32_t min_gap_us = 4000) {
    for (const auto &piece : split_burst(frame, min_gap_us)) {
      if (this->push(piece))
        return true;
    }
    return false;
  }

  /// The frame that was seen repeatedly, valid after push() returned true.
  const std::vector<int32_t> &code() const {
    static const std::vector<int32_t> none;
    return this->best_ < this->frames_.size() ? this->frames_[this->best_] : none;
  }
  /// How often it was seen.
  size_t repeats() const { return this->matches_; }

 protected:
  size_t needed_;
  size_t max_frames_;
  size_t min_pulses_;
  std::vector<std::vector<int32_t>> frames_;
  size_t matches_{0};
  size_t best_{0};
};

}  // namespace esphome::bl3361
