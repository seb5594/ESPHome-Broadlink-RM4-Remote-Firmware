#pragma once

// Single producer / single consumer queue between the pin interrupt and the
// main loop. The interrupt only stores how long the line stayed at the level
// it just left; all interpretation happens later in PulseFramer.
//
// One entry is a 32 bit word: the duration in microseconds shifted left by
// one, with the level in bit 0. That limits a duration to about 35 minutes,
// which is saturated, not wrapped. No locks: the producer only writes `head_`,
// the consumer only writes `tail_`.

#include <cstddef>
#include <cstdint>

namespace esphome::bl3361 {

/// Microseconds from `earlier` to `now`, never negative.
///
/// On the RTL8720C the interval between two edges sometimes came out negative
/// (probably micros() stepping back by a few microseconds). Subtracting as
/// unsigned numbers then gives about 71 minutes, which shows up in a received
/// frame as the marker +1073741823 and ends it there: a transmission was cut in
/// two at a random place. Such a step is a glitch, not time, and counts as 0
/// (the framer folds it away).
inline uint32_t elapsed_us(uint32_t now, uint32_t earlier) {
  const int32_t difference = static_cast<int32_t>(now - earlier);
  return difference < 0 ? 0 : static_cast<uint32_t>(difference);
}

template<size_t CAPACITY> class EdgeRing {
  static_assert((CAPACITY & (CAPACITY - 1)) == 0, "capacity must be a power of two");

 public:
  /// Interrupt side. Drops the entry and counts it if the queue is full.
  void push(uint32_t duration_us, bool level) {
    const size_t head = this->head_;
    const size_t next = (head + 1) & (CAPACITY - 1);
    if (next == this->tail_) {
      this->overflows_ = this->overflows_ + 1;
      return;
    }
    const uint32_t capped = duration_us > 0x7FFFFFFF ? 0x7FFFFFFF : duration_us;
    this->entries_[head] = (capped << 1) | (level ? 1u : 0u);
    this->head_ = next;
  }

  /// Main loop side. Returns false when the queue is empty.
  bool pop(uint32_t *duration_us, bool *level) {
    const size_t tail = this->tail_;
    if (tail == this->head_)
      return false;
    const uint32_t entry = this->entries_[tail];
    *duration_us = entry >> 1;
    *level = (entry & 1u) != 0;
    this->tail_ = (tail + 1) & (CAPACITY - 1);
    return true;
  }

  void clear() { this->tail_ = this->head_; }

  uint32_t overflows() const { return this->overflows_; }

 protected:
  volatile uint32_t entries_[CAPACITY]{};
  volatile size_t head_{0};
  volatile size_t tail_{0};
  volatile uint32_t overflows_{0};
};

}  // namespace esphome::bl3361
