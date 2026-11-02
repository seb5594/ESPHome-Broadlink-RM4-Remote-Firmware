#pragma once

// Wire protocol between the Wi-Fi chip (RTL8720CF) and the BL3361 RF module of
// the Broadlink RM4 Pro. EXPERIMENTAL: reconstructed from a flash dump of the
// stock firmware, never confirmed against a live device by this project yet.
//
// How it was found
// ----------------
// The stock firmware ("dna_rmctrl" framework, driver "bl3361") talks to the RF
// module over UART0 at 115200 baud, 8 data bits, even parity, 1 stop bit. The
// driver is a table of seven operations (init, deinit, open, set_channel,
// close, get_rssi, set_rssi_threshold). Every operation builds one request
// frame, writes it, and waits up to 300 ms for a reply frame whose command id
// is the request id plus one. Nothing else (no reset line, no power line) is
// switched by the driver; the profile stored in the flash lists only a single
// extra pin for the module, the raw OOK data pin (PA20).
//
// Frame layout (all numbers little endian):
//
//   offset  size  field
//   0       4     magic 0x5A5AA5A5 (bytes A5 A5 5A 5A)
//   4       2     checksum
//   6       2     command id
//   8       2     payload length
//   10      2     reserved, always 0
//   12      n     payload
//
// The checksum is 0xBEAF plus the sum of every byte of the frame, computed
// while the checksum field itself is zero, truncated to 16 bits.
//
// The functions below are a clean re-implementation of that description. They
// have no dependency on ESPHome, so they can be tested on a PC.

#include <cstddef>
#include <cstdint>

namespace esphome::bl3361 {

constexpr uint32_t FRAME_MAGIC = 0x5A5AA5A5;
constexpr uint16_t CHECKSUM_SEED = 0xBEAF;
constexpr size_t FRAME_HEADER_SIZE = 12;
// The stock driver receives into a 512 byte buffer and sends at most one
// configuration blob (204 bytes), so this is plenty.
constexpr size_t FRAME_MAX_PAYLOAD = 244;
constexpr size_t FRAME_MAX_SIZE = FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD;

/// Request ids understood by the module. A reply carries `id + 1`.
enum Command : uint16_t {
  /// Payload: configuration blob, see radio_config.h. Sent before every mode change.
  COMMAND_CONFIGURE = 0x600,
  /// Payload: channel (1 byte), direction (1 byte: 1 = receive, 2 = transmit).
  COMMAND_SET_CHANNEL = 0x602,
  /// Payload: threshold (1 byte). Only used by the stock frequency scan.
  COMMAND_SET_RSSI_THRESHOLD = 0x604,
  /// No payload. The reply carries one byte, the relative signal strength.
  COMMAND_GET_RSSI = 0x606,
  /// No payload. Starts receiving; the demodulated signal appears on the data pin.
  COMMAND_ENTER_RX = 0x608,
  /// No payload. Starts transmitting; the level of the data pin is sent as OOK.
  COMMAND_ENTER_TX = 0x60A,
  /// No payload. Sent by the stock driver at start-up and when it closes the module.
  COMMAND_IDLE = 0x60E,
};

/// Direction byte of COMMAND_SET_CHANNEL.
enum Direction : uint8_t {
  DIRECTION_RX = 1,
  DIRECTION_TX = 2,
};

constexpr uint16_t reply_id(uint16_t command) { return static_cast<uint16_t>(command + 1); }

/// Sum of all bytes plus the seed. The checksum field of `frame` must be zero.
inline uint16_t compute_checksum(const uint8_t *frame, size_t size) {
  uint32_t sum = CHECKSUM_SEED;
  for (size_t i = 0; i < size; i++)
    sum += frame[i];
  return static_cast<uint16_t>(sum);
}

inline void put_u16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value);
  dst[1] = static_cast<uint8_t>(value >> 8);
}

inline uint16_t get_u16(const uint8_t *src) { return static_cast<uint16_t>(src[0] | (src[1] << 8)); }

/// Writes a complete frame into `out` and returns its size, or 0 if it does not fit.
inline size_t encode_frame(uint16_t command, const uint8_t *payload, size_t payload_size, uint8_t *out,
                           size_t out_capacity) {
  if (payload_size > FRAME_MAX_PAYLOAD || out_capacity < FRAME_HEADER_SIZE + payload_size)
    return 0;
  out[0] = 0xA5;
  out[1] = 0xA5;
  out[2] = 0x5A;
  out[3] = 0x5A;
  put_u16(out + 4, 0);
  put_u16(out + 6, command);
  put_u16(out + 8, static_cast<uint16_t>(payload_size));
  put_u16(out + 10, 0);
  for (size_t i = 0; i < payload_size; i++)
    out[FRAME_HEADER_SIZE + i] = payload[i];
  const size_t total = FRAME_HEADER_SIZE + payload_size;
  put_u16(out + 4, compute_checksum(out, total));
  return total;
}

/// Collects the bytes of one reply frame. Feed it every received byte.
///
/// It resynchronises on the magic number, so stray bytes in front of a frame
/// (for example a boot message of the module) are skipped. A frame with a
/// wrong checksum is dropped and counted. A reply without payload counts as
/// complete without its two reserved bytes at the end.
class FrameDecoder {
 public:
  /// Returns true once a valid frame is complete. Call reset() before the next one.
  bool push(uint8_t byte) {
    if (this->complete_)
      return true;
    if (this->size_ < 4) {
      // Hunt for the four magic bytes. A byte that breaks the pattern may still
      // be the start of a new one, so a run of 0xA5 keeps the first two slots.
      static constexpr uint8_t MAGIC[4] = {0xA5, 0xA5, 0x5A, 0x5A};
      if (byte == MAGIC[this->size_]) {
        this->buffer_[this->size_++] = byte;
      } else if (byte == MAGIC[0]) {
        const bool run_of_first_byte = this->size_ > 0 && this->buffer_[this->size_ - 1] == MAGIC[0];
        this->size_ = run_of_first_byte ? 2 : 1;
        this->buffer_[0] = this->buffer_[1] = MAGIC[0];
      } else {
        this->size_ = 0;
      }
      return false;
    }
    this->buffer_[this->size_++] = byte;
    // A reply without payload is complete once its length field is in: the
    // reserved field behind it is always zero and adds nothing to the checksum.
    // A module that cuts the end of its replies off (seen on a device, it
    // persists until the power is cut) is understood this way. If the two bytes
    // do follow, the next request throws them away together with any other leftover.
    if (this->size_ == FRAME_HEADER_SIZE - 2 && get_u16(this->buffer_ + 8) == 0) {
      this->buffer_[10] = 0;
      this->buffer_[11] = 0;
      if (this->checksum_matches_()) {
        this->complete_ = true;
        return true;
      }
    }
    if (this->size_ < FRAME_HEADER_SIZE)
      return false;
    const size_t payload_size = get_u16(this->buffer_ + 8);
    if (payload_size > FRAME_MAX_PAYLOAD) {
      this->restart_();
      return false;
    }
    if (this->size_ < FRAME_HEADER_SIZE + payload_size)
      return false;
    if (this->checksum_matches_()) {
      this->complete_ = true;
      return true;
    }
    this->checksum_errors_++;
    this->restart_();
    return false;
  }

  void reset() { this->restart_(); }

  uint16_t command() const { return get_u16(this->buffer_ + 6); }
  const uint8_t *payload() const { return this->buffer_ + FRAME_HEADER_SIZE; }
  size_t payload_size() const { return get_u16(this->buffer_ + 8); }
  uint32_t checksum_errors() const { return this->checksum_errors_; }

 protected:
  void restart_() {
    this->size_ = 0;
    this->complete_ = false;
  }

  bool checksum_matches_() {
    const size_t total = FRAME_HEADER_SIZE + get_u16(this->buffer_ + 8);
    const uint16_t received = get_u16(this->buffer_ + 4);
    put_u16(this->buffer_ + 4, 0);
    const uint16_t expected = compute_checksum(this->buffer_, total);
    put_u16(this->buffer_ + 4, received);
    return expected == received;
  }

  uint8_t buffer_[FRAME_MAX_SIZE]{};
  size_t size_{0};
  bool complete_{false};
  uint32_t checksum_errors_{0};
};

}  // namespace esphome::bl3361
