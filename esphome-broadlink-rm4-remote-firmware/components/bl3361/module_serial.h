#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::bl3361 {

// The serial link to the RF module, opened the way the stock firmware does it:
// UART0 of the RTL8720CF with TX on PA14 and RX on PA13, at 115200 baud, 8 data
// bits, even parity and one stop bit. The pins and the format were read from
// the UART driver of a stock firmware dump, see docs/rf-protocol.md.
//
// ESPHome's `uart` component cannot be used for this. On the RTL8720C, UART0
// can be routed to two pairs of pins, so LibreTiny leaves PIN_SERIAL0_TX and
// PIN_SERIAL0_RX undefined. The component compares its pins with exactly these
// names, finds no match for PA14/PA13, and ends up on the software serial that
// does not exist on this chip. The port is opened here through the SDK of the
// chip instead, which names the pins directly, and it is read by polling, see
// module_serial.cpp.
class ModuleSerial {
 public:
  static constexpr uint32_t BAUD_RATE = 115200;

  /// Opens the port. Safe to call again.
  void open();
  /// Non-zero if a byte can be read. The port is polled, there is no buffer behind it.
  size_t available();
  /// Reads one byte, false if there is none.
  bool read_byte(uint8_t *byte);
  void write_array(const uint8_t *data, size_t size);
  /// Waits until everything has been sent.
  void flush();
};

}  // namespace esphome::bl3361
