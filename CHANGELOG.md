# Changelog

## 1.3.0 - 2026-11-02

### Changed

- **One event per transmission**: `esphome.<name>_rf_received` used to carry
  whatever the frame buffer held, so one key press gave a dozen events of up to
  512 pulses, cut at random places, with long preambles in between. The device
  now cuts the signal at its pauses and reports each transmission once, without
  the preamble that was cut off and without the rest at the end that is no
  complete transmission. The same code again within `repeat_timeout` (default
  500 ms, restarted by every repeat, so a held key is one event) is taken as a
  repeat and not reported; `repeat_timeout: 0ms` reports every transmission.
  The event keeps its data. Learning is not affected.
- **Modular layout**: one file per Broadlink model in `devices/rm4/`, and the
  functions as features, one file each, which get their pins as substitutions
  from the model:
  - `features/ir.yaml` (was `common/ir-proxy.yaml`): `pin_ir_tx`, `pin_ir_rx`
  - `features/controls.yaml` (was `common/controls.yaml`): `pin_status_led`,
    `pin_button`
  - `features/rf.yaml` (was `devices/rm4/rf.yaml`): `pin_rf_data`
  - `features/none.yaml` stands in for an optional feature that is switched off
  - `common/rtl8720c.yaml` and `common/irq_read.c` (were in
    `platform/rtl8720c/`) next to `common/base.yaml`
  A model puts its pin map into the substitutions and lists the features. How a
  new model is added is in [Adding a model](docs/adding-a-model.md).
- **The radio is a feature of `pro.yaml`** that is on by default. `feature_rf:
  none` in the `substitutions:` of your config leaves it out for an
  infrared-only RM4 Pro, which then needs no ESPHome 2026.10 either (that
  requirement moved from `pro.yaml` into the radio feature).

### Removed

- `devices/rm4/pro-ir.yaml` (new in 1.2.0): use `pro.yaml` with
  `feature_rf: none`.
- `devices/rm4-mini/rm4-mini.yaml` and `devices/rm4-pro/` (release 1.1.0): use
  `devices/rm4/mini.yaml` and `devices/rm4/pro.yaml`. The old `rm4-pro.yaml` was
  infrared only, so with it `feature_rf: none` is the same device; the old
  `rf.yaml` next to it is part of `pro.yaml` now and is not listed any more.
- `devices/rm4/rf.yaml`, `common/ir-proxy.yaml`, `common/controls.yaml`,
  `platform/rtl8720c/`: moved as listed above. If you built your own device file
  from them, point it at the new places.

### Added

- The `repeat_timeout` option of the `bl3361` component.
- Host tests for the receive filter.
- CI checks the RM4 Pro with the radio switched off as a remote package.

## 1.2.5 - 2026-11-02

### Fixed

- A `button:` entity of your own, such as one that sends a learned code, made the
  configuration invalid: the physical button of the package had the ID `button`,
  which is the name of an ESPHome integration. The ID is now `device_button`.
  (A config that referred to the ID `button` of the package has to use the new
  name.)

### Changed

- The CI configuration of the RM4 Pro contains a lock and a button that send
  codes, so this kind of addition is checked on every change.

## 1.2.4 - 2026-11-02

### Fixed

- **The cause of the cut-short replies**: the receive interrupt of the Arduino
  `Serial` class of the chip does not always run for the last byte of a reply.
  That byte stayed in the receive FIFO until the next reply arrived and showed up
  in front of it (the values `14`, `13`, `27` in front of the frames in the log
  were the signal strengths of the readings before). Every reply looked one byte
  short, the signal strength could not be read, and the frequency search and
  `rf_learn` failed. The module is not at fault, and it does not need a power
  cycle. The component now reads the UART of the chip by polling, through the
  SDK, and no longer depends on the interrupt.
- The attempt of 1.2.2 to bring the module back into step (a frame of zeros after
  a request without an answer) is gone again: the module never lost its framing.
  A request without an answer is sent once more, nothing else.

## 1.2.3 - 2026-11-02

### Fixed

- A module whose replies come without their last bytes (`No valid reply to
  command 0x600 ... 11 bytes arrived`, seen after a frequency search on a
  device) is understood again: a reply without payload counts as complete once
  its length field has arrived. Receiving and sending work in that state. The
  signal strength cannot be read then, because its value is the last byte. This
  state was caused by the serial link of the chip, see 1.2.4.
- A lost signal strength reading no longer holds the main loop for 300 ms every
  second. After a lost reading the sensor waits 30 s before it asks again.

## 1.2.2 - 2026-11-02

### Fixed

- The frequency search no longer ends because the module missed one answer. A
  reading that gets no answer is skipped, the tuning of a window is tried three
  times, and only five lost readings in a row end the search. The log now says
  where it stopped.
- A module that was left in the middle of a frame (a reset of the chip while it
  was writing to the UART) answered every later request with a broken frame. A
  request without an answer is now repeated once after the component has filled
  up such a frame, which lets the module find the start of the next one again.
  If it does not help, unplug the device for ten seconds: that resets the module.

### Added

- The log shows what reset the chip at start (`The chip was last reset by: ...`),
  to tell a crash from a watchdog or a power loss.
- [A car key as a lock](docs/rf-home-assistant.md#a-car-key-as-a-lock): a `lock`
  entity with lock, unlock and trunk (`open`) for a remote with a fixed code.

## 1.2.1 - 2026-11-02

### Fixed

- The dashboard reported `Platform not found: 'radio_frequency.bl3361'` after
  updating from 1.1.0 or an earlier 1.2.0 build. ESPHome keeps its copy of the
  radio component for `refresh: 1d`, and a package that is refreshed on every
  build did not reach that copy. The interval is now the substitution
  `firmware_refresh`; set it to `always` once to fetch the current component, see
  [Troubleshooting](docs/troubleshooting.md).

## 1.2.0 - 2026-11-02

The 433 MHz radio of the RM4 Pro works with a device for the first time: the
module answers and frames are received. The Pro now brings the radio along, the
device files moved, and a shared MAC address of two devices can be fixed.

### Added

- **The radio is part of the RM4 Pro**: `devices/rm4/pro.yaml` includes it, so
  there is no `rf.yaml` to add any more. `devices/rm4/pro-ir.yaml` is the
  infrared-only variant. The radio needs ESPHome 2026.10.0 or newer (the Mini and
  `pro-ir.yaml` stay at 2026.9.0).
- **Native `radio_frequency` entity** `RF Transmitter`: Home Assistant's own
  entity type for sub-GHz transmitters, through which other integrations send via
  the radio. It only sends. ESPHome marks the interface as experimental and it
  changed in 2026.10, which is why the radio needs that release.
- **Frequency entities**: `RF Frequency`, a choice (`315 MHz` and `433.92 MHz`
  by default, `options:` for your own, `Custom` for one that is not in the list)
  and `RF Custom Frequency`, a number for any value in MHz. The last frequency is
  kept over restarts.
- **Learning like the Broadlink app**: the actions `rf_learn`, `rf_find_frequency`
  and `rf_cancel`. The radio sweeps both bands while a key is held (the two
  500 kHz requests of the stock firmware, refined in 100 kHz and 10 kHz steps),
  tunes to what it found and captures a signal that repeats. The steps come as the
  event `esphome.<name>_rf_learn`.
- `rf_send_raw_at` sends on another frequency for one code, `frequency_mhz` is in
  the event `esphome.<name>_rf_received`, and `settle_time` and `sweep_margin` are
  options of `bl3361:`.
- [Using the radio in Home Assistant](docs/rf-home-assistant.md): a step-by-step
  guide with the learning automation, switches and buttons for learned codes,
  your own frequencies, a dashboard card, and what HAIR and the Broadlink
  integration can and cannot do for RF (HAIR is infrared only).
- The `mac_address` substitution. Some BL3357 modules have no MAC address in
  the chip, so the Wi-Fi driver gives every one of them `00:e0:4c:b7:23:00` and
  an RM4 Pro and an RM4C Mini on the same network ended up sharing MAC and IP
  address. With the real address in `mac_address` each device uses its own, see
  [the guide](docs/configuration.md#same-mac-address-on-several-devices).
  Confirmed on an RM4 Pro and an RM4C Mini.
- Host tests for the frequency search, the detection of repeated frames, the
  settle window and the time handling; CI builds every device and the paths of
  earlier releases.

### Changed

- **The device files moved** to `devices/rm4/`: `mini.yaml`, `pro.yaml`,
  `pro-ir.yaml` and `rf.yaml`. The old paths (`devices/rm4-mini/rm4-mini.yaml`,
  `devices/rm4-pro/rm4-pro.yaml`, `devices/rm4-pro/rf.yaml`) and the root file
  `broadlink-rm4-mini.yaml` still work and forward to the new files; the old
  `rm4-pro.yaml` stays infrared only, as it was. The example configs use the new
  paths.
- The packages build with a LibreTiny branch that fixes the MAC address
  handling (`platform/rtl8720c/rtl8720c.yaml`) instead of the latest release.
  This goes back to a release as soon as one contains the fix.
- When the module does not answer, the log says whether nothing or something
  that is no valid frame arrived, and shows the first bytes. `level: VERBOSE`
  also logs every frame sent.

### Fixed

- **The 433 MHz radio of the RM4 Pro never reached the module.** ESPHome's `uart`
  component cannot open UART0 of the RTL8720C on `PA14`/`PA13`: the chip can
  route that UART to two pairs of pins, so LibreTiny leaves the default pins
  undefined and the component fell back to a software serial that does not
  exist (`uart is marked FAILED: SoftwareSerial is not implemented for this
  chip`). The component `bl3361` now opens `Serial0` itself, with the pins and
  the format the stock firmware uses. `rf.yaml` no longer has a `uart:` section;
  a `uart:` of your own for the radio (and `uart_id` in `bl3361:`) has to go.
  The pins and the format are confirmed from the stock firmware dump, see
  [RF module protocol](docs/rf-protocol.md#the-uart-protocol).
- **Frames were cut in two at random places.** The interval between two edges
  sometimes came out negative and was read as 71 minutes, which ended the frame;
  it shows in the log as the marker `+1073741823`. Such an interval counts as 0
  now.
- **Noise right after tuning** produced a handful of frames and events.
  Everything in the first 1.5 s after tuning is dropped (`settle_time`).

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
