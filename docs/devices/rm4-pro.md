# ESPHome for the Broadlink RM4 Pro

Part of [ESPHome-Broadlink-RM4](../../README.md).

The RM4 Pro is the RM4 Mini with an additional 433 MHz radio. It uses the same
Wi-Fi chip, so infrared runs on the same layers as on the
[RM4 Mini](rm4-mini.md). The radio sits behind its own module and is the
experimental part: it is optional, and infrared works without it.

- [Status](#status)
- [Supported hardware](#supported-hardware)
- [Pin map and how it was verified](#pin-map-and-how-it-was-verified)
- [Opening and flashing](#opening-and-flashing)
- [433 MHz radio](#433-mhz-radio)
- [Help wanted](#help-wanted)

## Status

| Feature | State |
|---|---|
| IR emitter and receiver | Built from the same layers as the RM4 Mini. All pins are confirmed in the stock firmware, see below. Not yet reported as tested on a device. |
| Status LED, button | Same as above. Polarity follows the RM4 Mini. |
| 433 MHz receive | **Experimental.** Written from the stock firmware, not confirmed on a device yet. |
| 433 MHz transmit | **Experimental.** Same, software-timed. |
| Temperature/humidity sensor (some models) | Not mapped. |

## Supported hardware

| | |
|---|---|
| Device | Broadlink RM4 Pro |
| Main board | `RM4_PRO V2.1.6` (2021-08-26), as reported by [phdindota](https://github.com/phdindota/broadlink-rm4pro-esphome) |
| Wi-Fi module | BL3357-P with a **Realtek RTL8720CF**, 2 MiB flash (the size of a dump of this device) |
| RF module | BL3361-1, a separate module with a 433 MHz transceiver |
| Platform | ESPHome via [LibreTiny](https://github.com/libretiny-eu/libretiny), board `generic-rtl8720cf-2mb-992k` |

Other models of the RM4 family with RF, for example the RM4C Pro, are
presumably built the same way but nobody has checked.

## Pin map and how it was verified

The pin map comes from [phdindota/broadlink-rm4pro-esphome](https://github.com/phdindota/broadlink-rm4pro-esphome).
This project did not take it on trust but checked it against a flash dump of a
stock RM4 Pro. The stock firmware keeps a hardware profile as a text in the
flash (at file offset `0x1C0152` of a 2 MiB dump):

```text
"rmbase":[[1,0,4,18,8,1,4,7],[9,0,90,20],15,19,1,[0],[0],0,0,0]
```

The firmware parses this list in a fixed order and prints it in its boot log,
using these field names (the code that does so is in the image):

| Part of the profile | Fields | Values |
|---|---|---|
| `[1,0,4,18,8,1,4,7]` ("irda") | is_support, capin_work_level, **carrier_pin**, **data_pin**, data_gpt, carrier_gpt, carrier_gpt_channel, **led_pin** | 1, 0, **4**, **18**, 8, 1, 4, **7** |
| `[9,0,90,20]` ("rf") | is_support, freq_range_start, freq_range_end, **data_pin** | 9, 0, 90, **20** |
| `15, 19, ...` ("rmparam") | **app_network_pin**, **app_keypad_pin**, ... | **15**, **19** |

Which gives:

| Function | Pin | Profile field | Used here |
|---|---|---|---|
| IR transmitter (carrier) | `PA4` | irda carrier_pin 4 | yes, feature `ir` |
| IR receiver (data) | `PA18` | irda data_pin 18 | yes |
| Status LED | `PA7` | irda led_pin 7 | yes, inverted (on = low) |
| Button | `PA19` | rmparam app_keypad_pin 19 | yes, inverted, pull-up |
| RF data | `PA20` | rf data_pin 20 | yes, feature `rf` |
| Network pin | `PA15` | rmparam app_network_pin 15 | no, also the RX line of the flashing UART |
| UART to the radio module | `PA14` / `PA13` | `uart_drv_open` of the stock driver, port 0 | yes, in the `bl3361` component, see [RF module protocol](../rf-protocol.md#how-the-radio-is-wired) |
| Boot mode | `PA0` | | pull high at power-up for download mode |

phdindota's table matches the profile in every pin that is used here. Two
points differ: the flash is 2 MiB, not 4 MiB, and `PA15` is called *network pin*
in the profile where phdindota lists a Wi-Fi LED (the stock firmware's name for
it, `app_network_pin`, does not say whether it is an LED or a button; it is left
alone either way). What a dump cannot prove is the polarity of the LED and the button; the pins are the same as on the RM4 Mini,
whose pin map comes from a live boot log, and so are the polarities.

You can run the same check on your device: the profile is plain text in a
dump made with `ltchiptool flash read`.

## Opening and flashing

The RM4 Pro is opened and flashed like the [RM4 Mini](rm4-mini.md#flashing),
with a different serial header. The [README](../../README.md#opening-your-device)
has the step-by-step guide for every device. What is known for the Pro:

- The serial port is the `WIFI DEBUG` header: adapter TX to pin 16 (RX2),
  adapter RX to pin 17 (TX2), plus GND.
- Download mode: bridge `PA0` (pin 21 of that header) to `3.3V`, then
  power-cycle the device.
- Name the backup `rm4pro-stock.bin`.

## 433 MHz radio

See the [433 MHz guide](../433mhz.md) for the setup and for how to test it
without owning a 433 MHz device, and [RF module protocol](../rf-protocol.md)
for how the radio module works and what is still open.

In short: the radio feature
[`bl3361.yaml`](../../esphome-broadlink-rm4-remote-firmware/features/rf/bl3361.yaml),
which `pro.yaml` includes, talks to the radio over UART0, switches it between
receive and transmit and passes the raw signal through `PA20`. A received code
arrives as a Home Assistant event with its pulse timings; `rf_send_raw` sends
one, and `rf_learn` learns a remote the way the Broadlink app does.

## Help wanted

The radio part is complete as far as the stock firmware shows, but only a
device can confirm it. The most useful reports:

1. Does the log show `Listening on 433.920 MHz`? If not, what does the line before it say?
2. Does a key fob or socket remote produce events?
3. Does `rf_send_raw` switch a 433 MHz socket?
4. The photos and screw positions of the RM4 Pro for the opening guide.

Please open an [issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues)
with the log.
