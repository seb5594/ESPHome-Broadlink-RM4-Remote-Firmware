#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::bl3361 {

/// Index of the option that stands for `frequency_khz`, or -1 if there is none.
/// A frequency a few kHz off still counts: the module tunes in 10 kHz steps, so
/// 433.92 MHz can come back as 433.920 or as the 433.925 a search found.
inline int find_option(const std::vector<uint32_t> &options_khz, uint32_t frequency_khz, uint32_t tolerance_khz = 5) {
  for (size_t i = 0; i < options_khz.size(); i++) {
    const uint32_t option = options_khz[i];
    if ((option > frequency_khz ? option - frequency_khz : frequency_khz - option) <= tolerance_khz)
      return static_cast<int>(i);
  }
  return -1;
}

}  // namespace esphome::bl3361
