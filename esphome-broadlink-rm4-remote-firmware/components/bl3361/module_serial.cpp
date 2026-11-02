#include "module_serial.h"

#ifdef USE_LIBRETINY

#include <Arduino.h>

// The SDK of the chip. Its UART driver is read here directly, see the note below.
extern "C" {
#include "hal_uart.h"
}

namespace esphome::bl3361 {

// LibreTiny numbers its pins like the chip does: 13 is PA13, 14 is PA14.
static constexpr uint8_t PIN_RX = 13;
static constexpr uint8_t PIN_TX = 14;

// Why not Serial0: its receive interrupt does not always run for the last byte
// of a reply. That byte then stays in the receive FIFO of the chip until the next
// bytes arrive and shows up in front of the next reply, so every reply looked one
// byte short (the signal strength, the last byte of its reply, was the one that
// went missing). This link is polled instead: the main loop reads the FIFO itself
// while it waits for a reply, and nothing depends on an interrupt.
static hal_uart_adapter_t uart;
static bool opened = false;

void ModuleSerial::open() {
  if (opened)
    return;
  hal_uart_init(&uart, PIN_TX, PIN_RX, nullptr);
  hal_uart_set_baudrate(&uart, BAUD_RATE);
  // Same translation of the Arduino format the Serial class does: 8E1.
  const uint32_t parity = (SERIAL_8E1 & SERIAL_PARITY_MASK) ^ 0b11;
  hal_uart_set_format(&uart, 8, parity, 1);
  hal_uart_reset_rx_fifo(&uart);
  opened = true;
}

size_t ModuleSerial::available() { return opened && hal_uart_readable(&uart) ? 1 : 0; }

bool ModuleSerial::read_byte(uint8_t *byte) {
  if (!opened || !hal_uart_readable(&uart))
    return false;
  char value;
  if (!hal_uart_rgetc(&uart, &value))
    return false;
  *byte = static_cast<uint8_t>(value);
  return true;
}

void ModuleSerial::write_array(const uint8_t *data, size_t size) {
  if (!opened)
    return;
  for (size_t i = 0; i < size; i++) {
    while (!hal_uart_writeable(&uart)) {
    }
    hal_uart_putc(&uart, data[i]);
  }
}

void ModuleSerial::flush() {
  if (!opened)
    return;
  while (uart.base_addr->tflvr_b.tx_fifo_lv != 0) {
  }
  // The last character is still being shifted out: 11 bits at 115200 baud.
  delayMicroseconds(100);
}

}  // namespace esphome::bl3361

#endif  // USE_LIBRETINY
