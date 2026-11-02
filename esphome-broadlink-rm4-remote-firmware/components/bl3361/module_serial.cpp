#include "module_serial.h"

#ifdef USE_LIBRETINY

#include <Arduino.h>

namespace esphome::bl3361 {

// LibreTiny numbers its pins like the chip does: 13 is PA13, 14 is PA14.
static constexpr pin_size_t PIN_RX = 13;
static constexpr pin_size_t PIN_TX = 14;

void ModuleSerial::open() { Serial0.begin(BAUD_RATE, SERIAL_8E1, PIN_RX, PIN_TX); }

size_t ModuleSerial::available() { return Serial0.available(); }

bool ModuleSerial::read_byte(uint8_t *byte) {
  const int value = Serial0.read();
  if (value < 0)
    return false;
  *byte = static_cast<uint8_t>(value);
  return true;
}

void ModuleSerial::write_array(const uint8_t *data, size_t size) { Serial0.write(data, size); }

void ModuleSerial::flush() { Serial0.flush(); }

}  // namespace esphome::bl3361

#endif  // USE_LIBRETINY
