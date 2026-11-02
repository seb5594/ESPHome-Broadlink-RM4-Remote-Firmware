/*
 * Reading interrupt-driven GPIO pins on the Realtek RTL8720C.
 *
 * As soon as an interrupt is attached to a pin, LibreTiny removes the pin's
 * plain GPIO mode, because the RTL8720C can't use both at once. If
 * digitalRead() is called on such a pin later, LibreTiny sets the pin up as a
 * GPIO input again and frees the interrupt on the way. ESPHome's
 * remote_receiver reads its pin from inside the interrupt handler, so the
 * receiver lost its interrupt on the very first edge and never saw a code.
 *
 * The firmware is linked with -Wl,--wrap=digitalRead (see the `esphome:`
 * block in rtl8720c.yaml), which routes every digitalRead() call
 * through the function below. Pins with an active interrupt are read through
 * their interrupt object, every other pin goes to LibreTiny's digitalRead()
 * unchanged. LibreTiny itself is not modified.
 *
 * The fix follows the LibreTiny patch from phdindota, who tracked this down
 * first. Thank you!
 * https://github.com/phdindota/broadlink-rm4mini-esphome
 */

#include <ArduinoPrivate.h>

#if !LT_RTL8720C
#error "irq_read.c is only meant for the Realtek RTL8720C"
#endif

/* The original digitalRead(), provided by the linker. */
PinStatus __real_digitalRead(pin_size_t pinNumber);

PinStatus __wrap_digitalRead(pin_size_t pinNumber) {
	PinInfo *pin = pinInfo(pinNumber);

	if (pin != NULL && pinEnabled(pin, PIN_IRQ)) {
		/* PinData always exists once an interrupt is attached. */
		PinData *data = pinData(pin);
		if (data->irq != NULL)
			return gpio_irq_read(data->irq) ? HIGH : LOW;
	}

	return __real_digitalRead(pinNumber);
}
