# Changelog

## Unreleased

### Added

- **Frequency entity** `RF Frequency` for Home Assistant: choose the frequency of
  the radio from a list (`315 MHz` and `433.92 MHz` by default, `options:` for
  your own). The last choice is kept over restarts. `Custom` shows a frequency
  that is not in the list.
- **Learning like the Broadlink app**: the actions `rf_learn`,
  `rf_find_frequency` and `rf_cancel`. The radio sweeps both bands while a key is
  held, tunes to what it found and captures a signal that repeats. The steps come
  as the event `esphome.<name>_rf_learn`; [the guide](docs/433mhz.md#learning-a-remote-like-the-broadlink-app)
  has an automation that shows them as notifications and prints the finished
  action.
- `rf_send_raw_at` sends on another frequency for one code, `frequency_mhz` is in
  the event `esphome.<name>_rf_received` now, and `sweep_margin` and
  `settle_time` are options of `bl3361:`.
- Host tests for the frequency search, the detection of repeated frames, the
  settle window and the time handling.

- The `mac_address` substitution. Some BL3357 modules have no MAC address in
  the chip, so the Wi-Fi driver gives every one of them `00:e0:4c:b7:23:00` and
  an RM4 Pro and an RM4C Mini on the same network ended up sharing MAC and IP
  address. With the real address in `mac_address` each device uses its own, see
  [the guide](docs/configuration.md#same-mac-address-on-several-devices).
  Confirmed on an RM4 Pro and an RM4C Mini.

### Changed

- The packages build with a LibreTiny branch that fixes the MAC address
  handling (`platform/rtl8720c/rtl8720c.yaml`) instead of the latest release.
  This goes back to a release as soon as one contains the fix.
- When the module does not answer, the log says whether nothing or something
  that is no valid frame arrived, and shows the first bytes. `level: VERBOSE`
  also logs every frame sent.

### Fixed

- **Frames were cut in two at random places.** The interval between two edges
  sometimes came out negative and was read as 71 minutes, which ended the frame;
  it shows in the log as the marker `+1073741823`. Such an interval counts as 0
  now.
- **Noise right after tuning** produced a handful of frames and events.
  Everything in the first 1.5 s after tuning is dropped (`settle_time`).
- **The 433 MHz radio of the RM4 Pro never reached the module.** ESPHome's `uart`
  component cannot open UART0 of the RTL8720C on `PA14`/`PA13`: the chip can
  route that UART to two pairs of pins, so LibreTiny leaves the default pins
  undefined and the component fell back to a software serial that does not
  exist (`uart is marked FAILED: SoftwareSerial is not implemented for this
  chip`). The component `bl3361` now opens `Serial0` itself, with the pins and
  the format the stock firmware uses. `rf.yaml` no longer has a `uart:` section;
  a `uart:` of your own for the radio (and `uart_id` in `bl3361:`) has to go.
- The pins and the format are confirmed from the stock firmware dump now, see
  [RF module protocol](docs/rf-protocol.md#the-uart-protocol).

## 1.1.0 - 2026-10-23

The repository grows from a single-device project into ESPHome Broadlink RM4,
with a second device and an experimental 433 MHz radio.

### Added

- **Broadlink RM4 Pro** (infrared). All pins are confirmed in the hardware
  profile of the stock firmware, which was read from a flash dump.
- **Experimental 433 MHz radio of the RM4 Pro**: the new external component
  `bl3361` and the optional package `rf.yaml`. It tunes the radio over its UART
  (305-340 MHz and 430-440 MHz), receives raw codes as Home Assistant events
  (`esphome.<name>_rf_received`) and sends them (`rf_send_raw`). Further
  actions: `rf_set_frequency` and `rf_scan`, plus a signal strength sensor.
  Written from the stock firmware, not confirmed on a device yet.
- Documentation: 433 MHz guide (testing without a device, detecting what is on
  the air, rules for transmitting), RF module protocol, a page per device, a
  device table and opening guides in the README.
- CI: host tests for the radio driver, a build of every device and the RF
  config, and a check that every device file works as a remote package.

### Changed

- The configuration is split into packages: `platform/rtl8720c` (chip
  specifics and the receiver fix), `common` (base, controls, IR proxy) and one
  folder per device under `devices`. They all live in the folder
  `esphome-broadlink-rm4-remote-firmware`; the example configs of the devices
  are in the repository root.
- The device file holds only the pin map and the identity, the shared layers
  read the pins from there.
- **Package users:** the RM4 Mini device file is now
  `esphome-broadlink-rm4-remote-firmware/devices/rm4-mini/rm4-mini.yaml`.
  Configs from 1.0.0 keep working: `broadlink-rm4-mini.yaml` in the root of the
  repository forwards to it. New configs should start from
  `rm4-mini.example.yaml`.
- The repository was renamed to `ESPHome-Broadlink-RM4-Remote-Firmware`. GitHub
  redirects the old name.

## 1.0.0 - 2026-10-14

First release.

- Broadlink RM4 Mini / RM4C Mini (board `RM4_MINI-MAIN-V4.0.1`, RTL8720CF) as
  a native Home Assistant IR proxy: IR emitter and IR receiver on the
  `infrared` platform via `ir_rf_proxy`, ready for HAIR and every other
  integration built on that platform.
- Transmitter on ESPHome's own `remote_transmitter` with hardware PWM carrier
  and non-blocking sending, no custom `ir_tx.h` any more.
- Receiver fix for the RTL8720C: `digitalRead()` is wrapped at link time so
  `remote_receiver` keeps its interrupt. Based on the LibreTiny patch from
  [phdindota/broadlink-rm4mini-esphome](https://github.com/phdindota/broadlink-rm4mini-esphome).
- Hardware config as an ESPHome package; a device needs only a few lines of
  YAML (see `example.yaml`).
- Status LED, physical button, fallback hotspot with captive portal.
- Guide for opening the case, UART wiring, stock firmware backup and flashing.
- CI: yamllint, config validation and a full build against ESPHome 2026.9.0
  and the current release, plus a check in the firmware binary that the
  receiver fix is active.

Requires ESPHome 2026.9.0 and Home Assistant 2026.6.0 or newer.
