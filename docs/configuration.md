# ESPHome configuration

Every device in this repository is built from the same small layers. A device
file only holds the pin map and the identity of the hardware, everything else
comes from the layers below it:

| Layer | File | Purpose |
|---|---|---|
| Your device | [`rm4-mini.example.yaml`](../rm4-mini.example.yaml), [`rm4-pro.example.yaml`](../rm4-pro.example.yaml) | Name, Wi-Fi, API key, hotspot password. Pulls in the device file straight from GitHub. |
| Device | [`devices/<device>/<device>.yaml`](../esphome-broadlink-rm4-remote-firmware/devices) | Pin map, name and project info. Used as a package and never edited by you. |
| 433 MHz radio (RM4 Pro, experimental) | [`devices/rm4-pro/rf.yaml`](../esphome-broadlink-rm4-remote-firmware/devices/rm4-pro/rf.yaml) | Optional. The UART to the radio module and the [`bl3361` component](../esphome-broadlink-rm4-remote-firmware/components/bl3361). |
| IR proxy | [`common/ir-proxy.yaml`](../esphome-broadlink-rm4-remote-firmware/common/ir-proxy.yaml) | IR receiver, IR transmitter and the Home Assistant `infrared` entities. |
| Controls | [`common/controls.yaml`](../esphome-broadlink-rm4-remote-firmware/common/controls.yaml) | Status LED and physical button. |
| Base | [`common/base.yaml`](../esphome-broadlink-rm4-remote-firmware/common/base.yaml) | API, OTA, fallback hotspot, captive portal. |
| Platform | [`platform/rtl8720c/rtl8720c.yaml`](../esphome-broadlink-rm4-remote-firmware/platform/rtl8720c/rtl8720c.yaml) | Chip specifics: board, logger, the [receiver fix](#the-receiver-fix). |

That keeps your own file at a few lines, and fixes in this repository reach
your device with the next build. `refresh: 1d` makes ESPHome check for a new
version once a day. If you want to stay on a fixed state, replace the `ref`
with a commit hash or tag.

The layers are plain ESPHome packages, so a new RM4 variant needs only a new
device file: pick the layers that fit, set the pins.

## Device file

```yaml
substitutions:
  name: broadlink-rm4-mini
  friendly_name: Broadlink RM4 Mini
  pin_ir_tx: PA4
  pin_ir_rx: PA18
  pin_status_led: PA7
  pin_button: PA19

packages:
  rtl8720c: !include ../../platform/rtl8720c/rtl8720c.yaml
  base: !include ../../common/base.yaml
  controls: !include ../../common/controls.yaml
  ir_proxy: !include ../../common/ir-proxy.yaml

esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  project:
    name: seb5594.broadlink-rm4-mini
    version: ${project_version}
```

`name` becomes the hostname and the base of all entity IDs, `friendly_name` is
what Home Assistant shows. Your own config can override both. The `pin_*`
substitutions are the only hardware knowledge the shared layers need.
`project` shows up in the device info in Home Assistant, so you can see which
version of this config runs on the device.

## Platform layer

### `esphome`

```yaml
esphome:
  min_version: 2026.9.0
  build_flags:
    - -Wl,--wrap=digitalRead
  includes:
    - irq_read.c
```

`min_version` stops the build on ESPHome releases older than 2026.9.0, the
ones whose transmitter crashes this chip. `build_flags` and `includes` add the
receiver fix described below. The path is resolved next to the package file, so
this also works when the package comes straight from GitHub.

### The receiver fix

Without help, the IR receiver on the RTL8720C stays silent. ESPHome's
`remote_receiver` attaches an interrupt to the receiver pin and reads the pin
level inside that interrupt with `digitalRead()`. LibreTiny, however, removes
the plain GPIO mode of a pin as soon as an interrupt is attached, because the
RTL8720C can't use both at once. The next `digitalRead()` call sets the pin up
as a GPIO input again and frees the interrupt on the way, so the receiver sees
the first edge of a code and then nothing at all.

[`irq_read.c`](../esphome-broadlink-rm4-remote-firmware/platform/rtl8720c/irq_read.c) fixes that
without touching LibreTiny:

- The linker option `-Wl,--wrap=digitalRead` sends every `digitalRead()` call
  of the firmware to `__wrap_digitalRead()` from that file.
- For a pin with an active interrupt, the wrapper reads the level through the
  interrupt object (`gpio_irq_read()`), which leaves the interrupt alone.
- Every other pin, like the button, goes to LibreTiny's original
  `digitalRead()` unchanged.

The idea comes from phdindota, who found the cause and fixed it with a patch
script for LibreTiny's source. That script edits the LibreTiny copy in the
shared PlatformIO folder, so it affects every LibreTiny build on the machine
and needs an absolute path in the ESPHome dashboard. The linker wrap keeps the
fix inside this firmware and works from a GitHub package. The CI of this
repository compiles the firmware and checks in the binary that the receiver
path really calls the wrapper.

### `rtl87xx`

```yaml
rtl87xx:
  board: generic-rtl8720cf-2mb-992k
  family: rtl8720c
  framework:
    version: latest
```

The RTL8720CF runs ESPHome through LibreTiny. The board
`generic-rtl8720cf-2mb-992k` matches the 2 MB flash layout of the BL3357-P
module. `family` is implied by the board; it stays in the file because
ESPHome then refuses a wrong board instead of building something that does
not boot. `version: latest` picks the newest LibreTiny release that ships with
the installed ESPHome version (v1.13.0 in ESPHome 2026.9), not a development
snapshot.

### `logger`

```yaml
logger:
  level: INFO
  baud_rate: 0
```

`INFO` keeps the logs short and saves CPU time. `baud_rate: 0` switches off the
hardware UART: logging over it interferes with the receiver interrupts on this
chip. The logs are still available over Wi-Fi in the ESPHome dashboard. For
debugging received codes, set `level: DEBUG` and enable `dump: raw` in the
receiver.

## Base layer

```yaml
api:

ota:
  - platform: esphome
    id: ota_esphome

wifi:
  ap:

captive_portal:
```

The native API connects the device to Home Assistant. The layer leaves keys
and passwords to your own config. The example files add an API encryption key
and switch on OTA encryption, which reuses that key (new in ESPHome 2026.9 and
cheaper on flash than a separate OTA password). The OTA entry has an ID, so your
config extends it with `!extend ota_esphome` instead of creating a second one.

If the device can't reach your Wi-Fi, it opens a fallback hotspot named after
the device. Connect to it and the captive portal lets you enter new Wi-Fi
credentials, so a new router never means opening the case again. Give the
hotspot a password (`fallback_ap_password` in the example), because the portal
also accepts firmware uploads.

## Controls layer

```yaml
status_led:
  pin:
    number: ${pin_status_led}
    inverted: true

binary_sensor:
  - platform: gpio
    id: button
    name: Button
    pin:
      number: ${pin_button}
      inverted: true
      mode: INPUT_PULLUP
```

The LED shows the ESPHome status: it blinks while something is wrong (no
Wi-Fi, no API connection) and stays off when everything is fine. The physical
button becomes a binary sensor in Home Assistant and can trigger any
automation.

## IR proxy layer

### `remote_receiver`

```yaml
remote_receiver:
  - id: ir_rx
    pin:
      number: ${pin_ir_rx}
    filter: 200us
    idle: ${ir_receiver_idle}
```

- `filter: 200us` drops pulses shorter than 200 µs. Ambient light (sun, some
  lamps) causes such short flashes; without the filter they end up as garbage
  codes.
- `idle` is the silence that ends a code. 25 ms separates the presses of TV and
  audio remotes cleanly. Air conditioners send one press as several frames
  with gaps of up to about 35 ms; if such codes arrive split into pieces, set
  `ir_receiver_idle: 80ms` in your own config (HAIR uses the same value).
- `buffer_size` is not set on purpose. ESPHome's default holds 1000 pulses,
  enough for the longest air conditioner codes. A fixed value would mean
  different things depending on the ESPHome version: from ESPHome 2026.10 on
  (in beta at the time of writing) it counts bytes, 4 per pulse; before that it
  counted pulses. The `buffer_size: 2048` found in older configs therefore
  shrinks from 2048 to 512 pulses on new releases.
- `dump: raw` (commented out) prints every received code to the log.

### `remote_transmitter`

```yaml
remote_transmitter:
  - id: ir_tx
    pin: ${pin_ir_tx}
    carrier_duty_percent: 50%
    non_blocking: true
```

The transmitter modulates its output with a 38 kHz carrier at 50 % duty, the
usual value for IR. Since ESPHome 2026.9 the carrier comes from a PWM
peripheral and the timing from a hardware timer. `non_blocking: true` lets the
transmission run in the background, so the API, Wi-Fi and the receiver stay
responsive even while a long air conditioner code goes out.

### `infrared`

```yaml
infrared:
  - platform: ir_rf_proxy
    id: ir_proxy_tx
    name: IR Emitter
    remote_transmitter_id: ir_tx

  - platform: ir_rf_proxy
    id: ir_proxy_rx
    name: IR Receiver
    remote_receiver_id: ir_rx
    receiver_frequency: 38kHz
```

This is what makes the device an IR proxy. Each entry becomes one entity on
Home Assistant's native `infrared` platform: the emitter sends raw timings that
Home Assistant (or an integration such as HAIR) hands over, the receiver passes
every captured code to Home Assistant. Codes are never compiled into the
firmware, so learning new devices needs no reflash. `receiver_frequency` does
not change the hardware, it only tells Home Assistant that the receiver
demodulates 38 kHz signals.

## 433 MHz radio layer (RM4 Pro, experimental)

[`rf.yaml`](../esphome-broadlink-rm4-remote-firmware/devices/rm4-pro/rf.yaml) is an optional package for the
RM4 Pro. It is not part of `rm4-pro.yaml`, so an infrared-only device stays
as small as it was:

```yaml
uart:        # talks to the radio module
  - id: rf_uart
    tx_pin: PA14
    rx_pin: PA13
    baud_rate: 115200
    parity: EVEN

bl3361:      # external component of this repository
  uart_id: rf_uart
  data_pin: PA20
  frequency: 433.92MHz
```

The component loads from this repository through `external_components`, with
`firmware_ref` choosing the branch or tag. Keep it equal to the `ref` of your
package. The component has its own options (`filter`, `idle`, `min_pulses`,
`rssi`, `on_receive`) and actions (`bl3361.send_raw`, `bl3361.set_frequency`,
`bl3361.scan`, `bl3361.start_receive`, `bl3361.stop`), all described in the
[433 MHz guide](433mhz.md). How the radio module works is written down in
[RF module protocol](rf-protocol.md).

## Same MAC address on several devices

Some of the Wi-Fi modules (BL3357) have no MAC address stored in the chip. The
driver then uses its built-in `00:e0:4c:b7:23:00` for every unit, and two
devices on one network end up with the same MAC and the same IP address. Check
the MAC of your device in the router or in the ESPHome log.

Give such a device its real address, which is printed on its label and shown in
the Broadlink app, with the substitution `mac_address` (twelve hex digits):

```yaml
substitutions:
  mac_address: "ec0bae9e21c1"
```

The address is only held in RAM, nothing is written to the chip, and removing
the line undoes it. The router sees the new address after the next reboot, so
expect a new DHCP lease.

The packages use a LibreTiny build that has this fix, see `framework` in
`platform/rtl8720c/rtl8720c.yaml`. If the stock firmware is still on the
device, you do not even need the line: LibreTiny reads the address from the
sector the stock firmware keeps it in. Flashing a full image over the stock
firmware erases that sector, so keep the backup of the stock firmware. The real
address is in it, the six bytes at offset `0x4008` (the sector at `0x4000`
starts with the text `CALD`).

Do not set a MAC address through `on_boot` and `WiFi.setMacAddress()` or
`wifi_set_mac_address()` in a lambda. In current LibreTiny releases those write
the address permanently into the chip's eFuse, with the wrong format on top.

## Customizing

Everything in the package can be changed from your own config:

```yaml
substitutions:
  name: rm4-living-room
  friendly_name: RM4 Living Room
  ir_receiver_idle: 80ms        # long air conditioner codes

packages:
  broadlink_rm4_mini:
    url: https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware
    ref: main
    files: [esphome-broadlink-rm4-remote-firmware/devices/rm4-mini/rm4-mini.yaml]

# Add components ...
sensor:
  - platform: wifi_signal
    name: Wi-Fi signal
    update_interval: 60s

# ... or change existing ones by their ID
binary_sensor:
  - id: !extend button
    on_press:
      - logger.log: "Button pressed"
```

`!extend <id>` changes an item of the package, `!remove <id>` drops it.
