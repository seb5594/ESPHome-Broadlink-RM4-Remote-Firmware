# ESPHome Broadlink RM4

[![Validate](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/actions/workflows/validate.yml/badge.svg)](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/actions/workflows/validate.yml)
![ESPHome](https://img.shields.io/badge/ESPHome-%E2%89%A5%202026.9.0-000000?logo=esphome)
![Home Assistant](https://img.shields.io/badge/Home%20Assistant-%E2%89%A5%202026.6.0-41BDF5?logo=homeassistant)
![License](https://img.shields.io/badge/license-MIT-green)

Local, cloud-free ESPHome firmware for the Broadlink RM4 family. A flashed
device shows up in Home Assistant as a native **infrared** emitter and
receiver on Home Assistant's `infrared` platform, so everything that builds on
it works right away, for example [HAIR](https://github.com/DAB-LABS/HAIR) or the
LG and Samsung infrared integrations. On the **RM4 Pro** an experimental
driver adds the **433 MHz radio**.

- [Supported devices](#supported-devices)
- [Quick start](#quick-start)
- [Opening your device](#opening-your-device)
- [Flashing](#flashing)
- [In Home Assistant](#in-home-assistant)
- [433 MHz radio (experimental)](#433-mhz-radio-experimental)
- [How it is built](#how-it-is-built)
- [Branches and versions](#branches-and-versions)
- [Documentation](#documentation)
- [Help wanted](#help-wanted)
- [Thanks](#thanks)

## Supported devices

| Device | Board | Chip | IR send / receive | 433 MHz | LED, button | Status | Guide |
|---|---|---|---|---|---|---|---|
| Broadlink **RM4 Mini** | `RM4_MINI-MAIN-V4.0.1` | RTL8720CF | yes / yes | no radio | yes | **works** | [RM4 Mini](docs/devices/rm4-mini.md) |
| Broadlink **RM4C Mini** | `RM4_MINI-MAIN-V4.0.1` | RTL8720CF | yes / yes | no radio | yes | **works** | [RM4 Mini](docs/devices/rm4-mini.md) |
| Broadlink **RM4 Pro** | `RM4_PRO V2.1.6` | RTL8720CF + BL3361 radio | yes / yes | receive and send, **experimental** | yes | pin map verified in the stock firmware, radio receives on a device, sending not confirmed yet | [RM4 Pro](docs/devices/rm4-pro.md) |
| RM4C Pro, other RM4 variants | unknown | probably RTL8720CF | probably | unknown | | **untested**, check the board marking first | [Help wanted](#help-wanted) |

Older generations (RM3, RM mini 3, RM Pro and RM Pro+) are built differently
and are not covered.

What each configuration gives you:

| Feature | RM4 Mini / RM4C Mini | RM4 Pro (`pro.yaml`) | RM4 Pro, infrared only (`feature_rf: none`) |
|---|---|---|---|
| Infrared emitter (`infrared` entity) | ✅ | ✅ | ✅ |
| Infrared receiver (`infrared` entity) | ✅ | ✅ | ✅ |
| Status LED, button | ✅ | ✅ | ✅ |
| Over-the-air updates, fallback hotspot | ✅ | ✅ | ✅ |
| 433 MHz receive (event with raw timings) | – | 🧪 experimental | – |
| 433 MHz send (`rf_send_raw` action, `radio_frequency` entity) | – | 🧪 experimental | – |
| Frequency as a choice or a number, kept over restarts | – | 🧪 experimental | – |
| Learning like the Broadlink app (`rf_learn`) | – | 🧪 experimental | – |
| 433 MHz signal strength, channel scan | – | 🧪 experimental | – |
| Temperature/humidity sensor of some models | not mapped | not mapped | not mapped |

## Quick start

1. [Open the case](#opening-your-device), [back up the stock firmware and flash](#flashing)
   once over a USB-UART adapter. Every later update goes over Wi-Fi.
2. Copy the example config of your device into the ESPHome dashboard and add
   the keys of [`secrets.yaml.example`](secrets.yaml.example) to your secrets:
   - [`rm4-mini.example.yaml`](rm4-mini.example.yaml) (RM4 Mini and RM4C Mini)
   - [`rm4-pro.example.yaml`](rm4-pro.example.yaml) (RM4 Pro)
3. Add the device in Home Assistant, see [In Home Assistant](#in-home-assistant).

You need ESPHome **2026.9.0** or newer and Home Assistant **2026.6.0** or newer.
The 433 MHz radio of the RM4 Pro needs ESPHome **2026.10.0**.

| What | Version | Why |
|---|---|---|
| ESPHome | **2026.9.0** or newer | First release whose `remote_transmitter` works on the RTL8720C: hardware PWM carrier ([esphome#18579](https://github.com/esphome/esphome/pull/18579)) and timer-driven, non-blocking sending ([esphome#18648](https://github.com/esphome/esphome/pull/18648)). Older releases crash the chip as soon as something is sent. Enforced by `min_version`. |
| Home Assistant | **2026.6.0** or newer | First release that creates the receiver entity for ESPHome IR proxies. Sending alone works from 2026.4.0. |
| [HAIR](https://github.com/DAB-LABS/HAIR) | optional | Admin UI to capture, organize and send IR codes. Any other consumer of the `infrared` platform works too. |

For the first flash you also need a USB-UART adapter with **3.3 V logic
levels** (CH340, CP2102 or FT232), a few jumper wires, ideally a soldering iron,
and [ltchiptool](https://github.com/libretiny-eu/ltchiptool).

## Opening your device

Unplug the device before you open it. All RM4 models run on 5 V USB power, so
there is no mains voltage inside, but the boards are small and the pads are
fragile. A plastic opening tool, a small Phillips screwdriver and good light
are all you need. Take a photo of the inside before you touch anything.

<details open>
<summary><b>RM4 Mini and RM4C Mini</b> (board <code>RM4_MINI-MAIN-V4.0.1</code>)</summary>

1. Peel the rubber pad off the bottom of the device. The screws are hidden
   underneath. Keep the pad, it goes back on afterwards.
2. Remove the screws and separate the two halves of the case.
3. The board is `RM4_MINI-MAIN-V4.0.1`; the marking is printed on the back of
   the PCB. Check it before you continue, other boards need other firmware.
4. The serial port is a row of six plated through-holes along the edge of the
   back: `IOA0`, `IOA13`, `3.3V`, `U2-TX`, `U2-RX` and `GND`. `5V`, `T4` and `T8`
   are next to it. Solder short wires to `U2-TX`, `U2-RX` and `GND`, or hold pin
   headers or pogo pins in place if you don't want to solder.

<p align="center">
  <img src="docs/images/pcb-back.jpg" alt="Back of the RM4_MINI-MAIN-V4.0.1 board with the test points IOA0, IOA13, 3.3V, U2-TX, U2-RX and GND along the right edge" width="420">
</p>

Photo of the board by [phdindota](https://github.com/phdindota/broadlink-rm4mini-esphome) (MIT).

</details>

<details>
<summary><b>RM4 Pro</b> (board <code>RM4_PRO V2.1.6</code>)</summary>

This guide is shorter on purpose: nobody has documented the mechanical side
of the RM4 Pro in a way this project could verify, and a guess could break a
case. What is known, and a safe way to find the rest:

1. Look at the underside for a rubber pad, rubber feet or a sticker. On the
   RM4 Mini the screws sit underneath such a pad; check the same places.
   Lift a pad from one edge with a fingernail or a plastic tool and put it back
   afterwards.
2. Remove any screws you find and work the case halves apart with a plastic
   tool along the seam. If it does not give, look for more screws or clips
   instead of forcing it.
3. Find the board marking `RM4_PRO V2.1.6` and the two modules: the Wi-Fi
   module **BL3357-P** (Realtek RTL8720CF) and the radio module **BL3361-1**.
4. The serial port is the **`WIFI DEBUG` header**, according to
   [phdindota](https://github.com/phdindota/broadlink-rm4pro-esphome):

   | Header pin | Signal | Connect to |
   |---|---|---|
   | 16 | RX2 | adapter TX |
   | 17 | TX2 | adapter RX |
   | 21 | `PA0` (boot mode) | `3.3V` while flashing, see [Flashing](#flashing) |
   | | GND | adapter GND |

If you have an RM4 Pro open on the table, photos and the positions of its
screws would complete this guide, see [Help wanted](#help-wanted).

</details>

<details>
<summary><b>Other RM4 variants</b> (RM4C Pro and so on)</summary>

Not tested. Before you buy an adapter or flash anything:

1. Open the device the way you would open the Mini or the Pro.
2. Read the marking of the board and of the Wi-Fi module. This firmware is
   for modules with a **Realtek RTL8720CF** (BL3357-P). Anything else needs a
   different firmware.
3. Make a [backup of the stock firmware](#flashing) before trying anything.
4. If it works, or does not, please tell in an
   [issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues).

</details>

## Flashing

The full, illustrated procedure with all details is in the
[RM4 Mini guide](docs/devices/rm4-mini.md#flashing); the RM4 Pro works the same
way with the serial header from above. In short:

1. **Wire the adapter.** Adapter GND to GND, and TX and RX crossed:

   | | Adapter TX goes to | Adapter RX goes to |
   |---|---|---|
   | RM4 Mini / RM4C Mini | `U2-RX` | `U2-TX` |
   | RM4 Pro | header pin 16 (`RX2`) | header pin 17 (`TX2`) |

   **Leave the adapter's 3.3 V unconnected** and power the RM4 from its own USB
   cable; the adapter cannot supply the Wi-Fi module.

   <p align="center">
     <img src="docs/images/uart-wiring.svg" alt="Wiring: U2-TX to adapter RX, U2-RX to adapter TX, GND to GND, and a bridge from IOA0 to 3.3V while flashing" width="760">
   </p>

2. **Enter download mode.** Bridge the boot pin (`IOA0` on the Mini, `PA0`/pin 21
   on the Pro) to `3.3V`. The RTL8720C enters download mode when this pin is
   **high** at power-up (not to GND like an ESP8266). Then unplug and replug the
   USB power.
3. **Back up the stock firmware.** It is the only way back.

   ```bash
   ltchiptool flash read -d /dev/ttyUSB0 rtl8720c rm4-stock.bin
   ```

4. **Build the firmware.** In the ESPHome dashboard: **+ New device**, then
   replace the config with your example file, **Install → Manual download →
   UF2 package**. Or on the command line:

   ```bash
   git clone https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware.git
   cd ESPHome-Broadlink-RM4-Remote-Firmware
   cp secrets.yaml.example secrets.yaml      # fill in your values
   esphome compile rm4-pro.example.yaml      # or rm4-mini.example.yaml
   ```

5. **Flash.** With the boot pin still bridged, power-cycle once more:

   ```bash
   ltchiptool flash write -d /dev/ttyUSB0 firmware.uf2
   ```

6. **First boot.** Remove the bridge, disconnect the adapter and power-cycle.
   The device joins your Wi-Fi and Home Assistant finds it within a minute.
   From now on, **Install → Wirelessly** updates it.

Trouble? See [Troubleshooting](docs/troubleshooting.md). To go back to Broadlink,
write the backup with `ltchiptool flash write`.

## In Home Assistant

After the first boot the device shows up under **Settings → Devices & services**.
Click **Add**, enter the API encryption key from your secrets, and you get:

| Entity | Example ID (RM4C Mini) | What it does |
|---|---|---|
| IR Emitter | `infrared.broadlink_rm4c_mini_ir_emitter` | Sends IR codes. Its state is the time of the last transmission. |
| IR Receiver | `infrared.broadlink_rm4c_mini_ir_receiver` | Receives IR codes. Its state is the time of the last received code. |
| Button | `binary_sensor.broadlink_rm4c_mini_button` | The physical button. |

Home Assistant lists all emitters and receivers under **Settings →
Connectivity → Infrared**. Integrations for IR-controlled devices, such as
[LG Infrared](https://www.home-assistant.io/integrations/lg_infrared/) or
[Samsung Infrared](https://www.home-assistant.io/integrations/samsung_infrared/),
ask for an emitter during setup; pick the RM4 there.

**HAIR** is the easiest way to learn a remote: install
[HAIR](https://github.com/DAB-LABS/HAIR) through HACS, add the integration, open
**HAIR** in the sidebar, press buttons of your remote at the RM4 in the
**Sniffer** tab and turn them into a device. Air conditioners need
`ir_receiver_idle: 80ms` in your config, otherwise one press can show up as
several signals. Codes you learned with the Broadlink integration can be moved
over with HAIR's **Plucker**. The details, including how automations change,
are in [Home Assistant and HAIR](docs/home-assistant.md).

## 433 MHz radio (experimental)

> **Experimental.** The radio driver was written from a flash dump of the
> stock firmware. The module answers and frames are received on a device,
> sending is not confirmed yet. Infrared does not depend on it.

The 433 MHz radio of the RM4 Pro is a separate module, BL3361-1. The Wi-Fi chip
tunes it over a UART and exchanges the raw signal with it through one data pin.
The external component [`bl3361`](esphome-broadlink-rm4-remote-firmware/components/bl3361)
implements that, and [`pro.yaml`](esphome-broadlink-rm4-remote-firmware/devices/rm4/pro.yaml)
brings it along with the infrared part, so there is nothing extra to include
(ESPHome 2026.10 or newer; `feature_rf: none` in your config leaves the radio out). In Home
Assistant you get:

- **RF Frequency** and **RF Custom Frequency**: the frequency of the radio as a
  choice and as any value in MHz (305-340 MHz and 430-440 MHz), kept over restarts.
- **Learning like the Broadlink app**: the action `rf_learn` finds the frequency
  while you hold a key and captures the code when you press it briefly.
- **Sending**: `rf_send_raw` and `rf_send_raw_at` send a code, the native
  **RF Transmitter** entity lets other integrations send through the radio.
- **Receiving**: every code that arrives is fired as the event
  `esphome.<name>_rf_received` with the pulse timings in `code`.
- `rf_scan` shows the signal strength of 20 channels around the frequency.

You do not need a 433 MHz device to try it, and there are cheap ways to see what
is on the air. The [433 MHz guide](docs/433mhz.md) covers:

- sources for a first test (key fob, sockets, ESP32 + FS1000A),
- reading signals without a known device (RTL-SDR + `rtl_433`, an ESP32 with an
  RXB6 receiver, Flipper Zero, the stock firmware as a baseline),
- finding out what other devices around you transmit,
- reading a Smartwares or other remote,
- the rules for transmitting.

How the radio module works, how it was found and what is still open is in
[RF module protocol](docs/rf-protocol.md). The pin map of the RM4 Pro was
verified against the stock firmware, see [RM4 Pro](docs/devices/rm4-pro.md#pin-map-and-how-it-was-verified).

## How it is built

The firmware is assembled from small ESPHome packages. There is one file per
model, which holds the identity and the pin map and puts the model together from
the shared parts. The features do the work and get their pins as substitutions:

```text
rm4-mini.example.yaml        your config: name, Wi-Fi, keys. Pulls the rest from GitHub
rm4-pro.example.yaml
esphome-broadlink-rm4-remote-firmware/
  devices/rm4/               one file per model: mini.yaml, pro.yaml
  features/                  one file per function, the pins come from the model
    ir.yaml                  infrared emitter and receiver (pin_ir_tx, pin_ir_rx)
    rf.yaml                  433 MHz radio of the BL3361 module (pin_rf_data)
    controls.yaml            status LED and button (pin_status_led, pin_button)
    none.yaml                stands in for a feature that is switched off
  common/                    base: API, OTA, hotspot; rtl8720c: board, logger, IR receiver fix
  components/bl3361/         external component for the 433 MHz radio
docs/                        guides: configuration, Home Assistant, 433 MHz, devices
.github/                     CI: lint, host tests, build for every device
```

| Layer | Purpose |
|---|---|
| Your device | Name, Wi-Fi, API key, hotspot password. |
| Model | Identity, pin map and the list of features. One file per Broadlink model. |
| Feature `ir` | `remote_receiver`, `remote_transmitter` and the `infrared` entities of `ir_rf_proxy`. |
| Feature `rf` | The radio: frequency entities, receive, send, learning. |
| Feature `controls` | Status LED and physical button. |
| Base | API, OTA, fallback hotspot and captive portal. |
| Chip | Board, logger and the receiver fix (`common/rtl8720c.yaml`). |

Two details worth knowing. The IR receiver does not work on the RTL8720C out of
the box, because LibreTiny drops the interrupt of a pin when it is read; a
linker wrap in [`irq_read.c`](esphome-broadlink-rm4-remote-firmware/common/irq_read.c)
fixes that without touching LibreTiny. And the hardware UART logger is off
(`baud_rate: 0`), because it interferes with the receiver interrupts; logs are
available over Wi-Fi. Everything is explained in
[ESPHome configuration](docs/configuration.md).

Another Broadlink needs one new file in `devices/` and, if it brings hardware
the features don't cover, a new feature folder; see
[Adding a model](docs/adding-a-model.md).

## Branches and versions

- `main` is the stable state. This is **v1.3.0**, see the [changelog](CHANGELOG.md).
- `dev` is where development happens. To try it, set `ref: dev` (and
  `firmware_ref: dev` for the RM4 Pro) in the package of your config.
- Configs written for an older release used other file names (`broadlink-rm4-mini.yaml`
  of **v1.0.0**, `devices/rm4-mini/` and `devices/rm4-pro/` of **v1.1.0**,
  `pro-ir.yaml` of **v1.2.0**). They are gone since v1.3.0, see the
  [changelog](CHANGELOG.md) for what to use instead, or take an example file.

Use a commit hash or tag as `ref` to stay on a fixed state.

## Documentation

- [ESPHome configuration](docs/configuration.md): layers, the receiver fix, customizing
- [Home Assistant and HAIR](docs/home-assistant.md)
- [433 MHz guide](docs/433mhz.md): setup, testing, detecting, rules
- [RF module protocol](docs/rf-protocol.md): how the radio module works
- Device pages: [RM4 Mini](docs/devices/rm4-mini.md), [RM4 Pro](docs/devices/rm4-pro.md)
- [Adding a model](docs/adding-a-model.md): how the files fit together, for another Broadlink
- [Releasing](docs/releasing.md): how a version is published
- [Using the radio in Home Assistant](docs/rf-home-assistant.md): frequency, learning, sending, a dashboard card
- [Troubleshooting](docs/troubleshooting.md)
- [Changelog](CHANGELOG.md)

## Help wanted

- **RM4 Pro owners**: does `Listening on 433.920 MHz` appear in the log? Do key fobs or socket remotes produce events? Photos of the
  open case and the position of its screws would finish the opening guide.
- **Other RM4 variants**: board marking, module, and whether it works.
- **Logic analyzer or SDR owners**: a capture of the UART while the stock
  firmware learns a code settles the open questions of the
  [RF protocol](docs/rf-protocol.md#what-is-not-confirmed).

[Open an issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues)
with the log.

## Thanks

A big thank you to **phdindota** and
[phdindota/broadlink-rm4mini-esphome](https://github.com/phdindota/broadlink-rm4mini-esphome).
Without that project this one wouldn't exist: it was the first to put ESPHome
on the RM4 Mini, worked out the pin map from the stock firmware, documented how
to open and flash the device, found the cause of the silent IR receiver and
provided the photo of the board used in the RM4 Mini guide (MIT licensed). The
RM4 Pro work builds on the notes in
[phdindota/broadlink-rm4pro-esphome](https://github.com/phdindota/broadlink-rm4pro-esphome).

Thanks as well to

- [LibreTiny](https://github.com/libretiny-eu/libretiny) for bringing ESPHome
  to the RTL8720CF,
- [ESPHome](https://esphome.io/) for the RTL8720C transmitter work and the IR
  proxy,
- [DAB-LABS](https://github.com/DAB-LABS) for [HAIR](https://github.com/DAB-LABS/HAIR).

## License

[MIT](LICENSE)
