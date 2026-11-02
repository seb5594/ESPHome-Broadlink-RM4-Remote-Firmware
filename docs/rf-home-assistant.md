# Using the radio in Home Assistant

How to use the 433 MHz radio of the RM4 Pro from Home Assistant, step by step.
The radio is **experimental**: the module answers and frames are received on a
device, learning and sending are not confirmed yet. Infrared is unaffected. For
the hardware side, tests without a 433 MHz device and the rules for
transmitting see the [433 MHz guide](433mhz.md).

- [What you need](#what-you-need)
- [What the device brings](#what-the-device-brings)
- [Infrared or radio: nothing to switch](#infrared-or-radio-nothing-to-switch)
- [First check](#first-check)
- [Choosing the frequency, and your own](#choosing-the-frequency-and-your-own)
- [Learning a remote](#learning-a-remote)
- [Using a learned code](#using-a-learned-code)
- [A car key as a lock](#a-car-key-as-a-lock)
- [Sending through Home Assistant's radio frequency platform](#sending-through-home-assistants-radio-frequency-platform)
- [Receiving](#receiving)
- [A dashboard for it](#a-dashboard-for-it)
- [HAIR and the Broadlink integration](#hair-and-the-broadlink-integration)
- [When something does not work](#when-something-does-not-work)

## What you need

- An RM4 Pro flashed with this firmware, and ESPHome **2026.10.0** or newer
  to build it. The device file is `pro.yaml`, which brings the radio along: you
  do **not** include `rf.yaml` yourself any more. See
  [`rm4-pro.example.yaml`](../rm4-pro.example.yaml).
- Home Assistant **2026.6.0** or newer, with the device added under **Settings →
  Devices & services** (ESPHome), as described in [Home Assistant](home-assistant.md).
- A remote or a device on 433 MHz to try it with, see
  [Testing without owning a 433 MHz device](433mhz.md#testing-without-owning-a-433-mhz-device).

An RM4 Pro that should stay infrared only uses `pro-ir.yaml` instead of
`pro.yaml` and has none of the following.

## What the device brings

The names depend on your `friendly_name`; the examples are for
`Broadlink RM4 Pro` with the name `broadlink-rm4-pro`.

| What | Example | Use |
|---|---|---|
| Select | `select.broadlink_rm4_pro_rf_frequency` | The frequency of the radio as a choice. |
| Number | `number.broadlink_rm4_pro_rf_custom_frequency` | Any frequency in MHz. |
| Sensor | `sensor.broadlink_rm4_pro_rf_signal_strength` | A raw, relative signal strength, diagnostic. |
| Radio frequency | `radio_frequency.broadlink_rm4_pro_rf_transmitter` | The native entity other integrations send through. |
| Event | `esphome.broadlink-rm4-pro_rf_received` | Every received code. |
| Event | `esphome.broadlink-rm4-pro_rf_learn` | The steps of a search or learning session. |
| Action | `esphome.broadlink_rm4_pro_rf_learn` | Learn a remote: hold a key, then press it briefly. |
| Action | `esphome.broadlink_rm4_pro_rf_find_frequency` | Only the first half: find the frequency. |
| Action | `esphome.broadlink_rm4_pro_rf_cancel` | End a search or learning session. |
| Action | `esphome.broadlink_rm4_pro_rf_send_raw` | Send a code on the current frequency. |
| Action | `esphome.broadlink_rm4_pro_rf_send_raw_at` | Send a code on another frequency, for this code only. |
| Action | `esphome.broadlink_rm4_pro_rf_set_frequency` | Tune to a frequency. |
| Action | `esphome.broadlink_rm4_pro_rf_scan` | Log the signal strength of 20 channels. |

Mind the difference in the names: the **events** carry the device name as it is
written in the config, with hyphens (`broadlink-rm4-pro`), the **actions** and
entities are the Home Assistant form with underscores.

## Infrared or radio: nothing to switch

There is no mode to switch. The infrared and the radio part of the RM4 Pro are
separate hardware that works at the same time, and each has its own entities:

| You want to | Use |
|---|---|
| Send or learn **infrared** (TV, air conditioner, ...) | The `infrared` entities of the device, with [HAIR](home-assistant.md#using-it-with-hair) or an integration such as LG Infrared. |
| Send or learn **433 MHz** (sockets, blinds, doorbells, ...) | The radio entities and actions of this page. |

A remote is either one or the other, so you pick the part that matches the
remote. The Broadlink app also kept them apart: it only has two kinds of
learning.

## First check

1. Open **Developer tools → States** and look for `RF Signal Strength`. It
   reports a value (around 20 to 23 without a signal): that proves the module
   answers. If it stays *unknown*, see [When something does not work](#when-something-does-not-work).
2. Open **Developer tools → Events**, listen to `esphome.broadlink-rm4-pro_rf_received`
   and press a 433 MHz remote about 10 cm from the device. Events with a
   `code` appear, and the signal strength rises to 30 or more while you press.
3. If you see nothing, the remote is probably on another frequency: see the
   next section and [Learning a remote](#learning-a-remote).

## Choosing the frequency, and your own

The radio listens on one frequency at a time: 305-340 MHz and 430-440 MHz are
possible, 868 MHz is not.

- **RF Frequency** is a choice. It offers `315 MHz` and `433.92 MHz` and, as the
  last entry, **Custom**, which only shows that the radio sits on a frequency
  that is not in the list.
- **RF Custom Frequency** is a number in MHz for **any** frequency, for example
  `433.42`. Type it in and confirm. A value the module cannot tune to is refused
  and the old one stays.
- The last frequency is **kept over restarts**. `rf_frequency` in your config
  only decides the first start, and changing it later does nothing: choose the
  frequency in Home Assistant once.

To get **your own frequencies into the choice**, list them in your device config:

```yaml
bl3361:
  frequency_select:
    options: [315MHz, 433.92MHz, 433.42MHz]
```

The list takes any frequency between 305.000 and 339.999 MHz and between 430.000
and 439.999 MHz; ESPHome stops the build and tells you if one is outside. The
module tunes in steps of 10 kHz around 315 and 433 MHz and of 100 kHz elsewhere,
so `433.42` is exact and `320.55` becomes `320.5`.

A third way is the action `esphome.broadlink_rm4_pro_rf_set_frequency` with
`frequency_mhz`, for automations.

## Learning a remote

The action `rf_learn` works like the learning of the Broadlink app. It runs in
two steps:

1. **Hold a key** of the remote, about 10 cm from the device, until the
   notification asks for the next step. The radio sweeps both bands and tunes to
   the frequency it finds (up to 30 s).
2. **Press the key briefly**, a few times. The first signal that comes back is
   the code (up to 20 s).

The result arrives as the event `esphome.broadlink-rm4-pro_rf_learn`. This
automation turns the steps into notifications and shows the finished action at
the end, ready to copy:

```yaml
automation:
  - alias: RF learning
    triggers:
      - trigger: event
        event_type: esphome.broadlink-rm4-pro_rf_learn
    variables:
      d: "{{ trigger.event.data }}"
    actions:
      - action: persistent_notification.create
        data:
          notification_id: rf_learn
          title: Learn an RF code
          message: >-
            {% if d.phase == 'sweep' %}
              Hold a key of the remote close to the RM4 Pro.
            {% elif d.phase == 'capture' %}
              Frequency found: {{ d.frequency_mhz }} MHz.
              Now press the key briefly, a few times.
            {% elif d.phase == 'learned' %}
              Learned on {{ d.frequency_mhz }} MHz, seen {{ d.repeats }} times.
              Paste this into a script, a switch or a button:

                  action: esphome.broadlink_rm4_pro_rf_send_raw_at
                  data:
                    frequency_mhz: {{ d.frequency_mhz }}
                    repeat: 8
                    code: [{{ d.code }}]
            {% elif d.phase == 'timeout' %}
              Nothing found. Try again, closer to the device.
            {% elif d.phase == 'cancelled' %}
              Cancelled.
            {% elif d.phase == 'failed' %}
              The RF module does not answer, see the log of the device.
            {% endif %}
```

Start it from a button on a dashboard or from **Developer tools → Actions** with
`esphome.broadlink_rm4_pro_rf_learn`. `rf_cancel` stops it. The phases are
`sweep`, `found`, `capture`, `learned`, `timeout`, `cancelled` and `failed`.

Notes:

- A key that is held repeats its code, and the receiver delivers many
  repetitions in one frame. The learning cuts such a frame at every pause of
  4 ms or more and looks for a piece that comes back, so one press can already
  be enough.
- A remote that sends a **different code every time** (a rolling code, as in
  car keys, garage doors and alarms) never comes back and cannot be learned.
  That is intended.
- If the sweep finds nothing although the remote is close, lower
  `sweep_margin` (default 3) in the `bl3361:` section. Every pass is in the log
  at `level: DEBUG`.
- If you know the frequency, skip the first step: set it, press the key and
  copy the code from the event `esphome.broadlink-rm4-pro_rf_received`.

## Using a learned code

A code is a list of microseconds (positive: signal on, negative: off). Put it
into whatever you use in Home Assistant. A **switch** for a 433 MHz socket with
an on and an off code:

```yaml
template:
  - switch:
      - name: Socket living room
        unique_id: rf_socket_living_room
        turn_on:
          - action: esphome.broadlink_rm4_pro_rf_send_raw_at
            data:
              frequency_mhz: 433.92
              repeat: 8
              code: [350, -1050, 1050, -350, 350, -1050, 350, -10850]
        turn_off:
          - action: esphome.broadlink_rm4_pro_rf_send_raw_at
            data:
              frequency_mhz: 433.92
              repeat: 8
              code: [350, -1050, 350, -1050, 350, -1050, 350, -10850]
```

The switch has no feedback: Home Assistant assumes the state it sent. For things
without a state (a doorbell, a blind that goes up or down) use a **button**:

```yaml
template:
  - button:
      - name: Blind up
        unique_id: rf_blind_up
        press:
          - action: esphome.broadlink_rm4_pro_rf_send_raw_at
            data:
              frequency_mhz: 433.92
              repeat: 10
              code: [350, -1050, 1050, -350, 350, -1050, 350, -10850]
```

`repeat` is how often the code is sent in a row (1 to 30); remotes repeat while a
key is held, and 8 to 10 is a good start. `rf_send_raw_at` tunes to
`frequency_mhz` for this code and goes back to the selected frequency, so it
works whatever the radio listens on. `rf_send_raw` sends on the current frequency.

Sending is not confirmed on a device yet; please try it with something harmless
first and report what you see, see [the rules for transmitting](433mhz.md#rules-for-transmitting).

## A car key as a lock

A remote with a lock, an unlock and a trunk key is best as one **lock** entity
plus a **button**. The device itself offers them, so they keep working when Home
Assistant is down and need no template in Home Assistant. Learn each key once
(see [Learning a remote](#learning-a-remote)), then put the codes into your
device config, below the `packages:` section:

```yaml
lock:
  - platform: template
    name: Car
    # The key gives no feedback, so the lock shows what was sent last.
    optimistic: true
    assumed_state: true
    lock_action:
      - bl3361.send_raw:
          frequency: 433.92MHz
          repeat: 8
          code: [350, -1050, 1050, -350, 350, -1050, 350, -10850]
    unlock_action:
      - bl3361.send_raw:
          frequency: 433.92MHz
          repeat: 8
          code: [350, -1050, 350, -1050, 350, -1050, 350, -10850]
    # Shows up as "Open" on the lock in Home Assistant. Many cars open the
    # trunk with its own key, which is what this is for.
    open_action:
      - bl3361.send_raw:
          frequency: 433.92MHz
          repeat: 8
          code: [350, -1050, 1050, -1050, 350, -1050, 350, -10850]
```

Use the frequency `rf_learn` reported for your key (for a key that was found on
434.3 MHz, `frequency: 434.3MHz`); every code is sent on its own frequency, no
matter what the radio is tuned to. If you prefer a plain button for the trunk,
add a `button: - platform: template` with `on_press:` and the same
`bl3361.send_raw` action instead of `open_action`.

Only keys with a **fixed code** work this way. A key that changes its code with
every press (rolling code, as most newer cars have) is not replayable: the car
ignores a code it has already heard. If the learned code differs every time you
press the same key, this is the reason.

## Sending through Home Assistant's radio frequency platform

Home Assistant has a native **Radio frequency** platform since 2026.5: an
entity that represents a sub-GHz transmitter, which **other integrations** use to
send. The release notes name integrations such as a Honeywell string lights and
a Novy cooker hood integration. The device offers it as **RF Transmitter**
(`radio_frequency.broadlink_rm4_pro_rf_transmitter`), so such an integration can
use the RM4 Pro: when you set it up, it asks which transmitter to use, and you
pick this one.

- It only sends. Received codes arrive as events, see below.
- It sends on the frequency the integration asks for. A request for a frequency
  the module cannot tune to (anything between 340 and 430 MHz) is refused.
- ESPHome marks this entity as experimental and its interface changes between
  releases, which is why the radio needs ESPHome 2026.10.

## Receiving

Every received code is fired as the event `esphome.broadlink-rm4-pro_rf_received`
with `code` (the timings, comma separated), `pulses` (their number) and
`frequency_mhz`. All values are text, so compare numbers with `| int`:

```yaml
automation:
  - alias: Doorbell
    triggers:
      - trigger: event
        event_type: esphome.broadlink-rm4-pro_rf_received
    conditions:
      - condition: template
        value_template: "{{ trigger.event.data.pulses | int > 40 }}"
    actions:
      - action: notify.notify
        data:
          message: Someone rang ({{ trigger.event.data.pulses }} pulses)
```

The device has no decoder, so there is no "this key was pressed" event: you see
the raw timings. Two different keys differ in their codes, and the same key comes
back with a few microseconds of jitter, so an exact comparison of the text is too
strict. A reliable way is to match on `pulses` and `frequency_mhz`, or to look at
a few of the timings after rounding them. If you need decoding of known
protocols (weather stations, tyre sensors), `rtl_433` with an RTL-SDR does that,
see [Finding out what is on the air](433mhz.md#finding-out-what-is-on-the-air).

Noise: right after the module is tuned it produces a burst of random pulses. The
first 1.5 s after tuning are dropped (`settle_time`), and `min_pulses` (default
24) drops short frames. If events fire for noise, raise `min_pulses` to just
below the pulse count of your remote.

## A dashboard for it

A card that gives a stranger everything they need: the frequency, the signal and
the learning. Replace the entity IDs with yours.

```yaml
type: vertical-stack
cards:
  - type: entities
    title: 433 MHz radio
    entities:
      - entity: select.broadlink_rm4_pro_rf_frequency
        name: Frequency
      - entity: number.broadlink_rm4_pro_rf_custom_frequency
        name: Other frequency (MHz)
      - entity: sensor.broadlink_rm4_pro_rf_signal_strength
        name: Signal
  - type: horizontal-stack
    cards:
      - type: button
        name: Learn a remote
        icon: mdi:remote
        tap_action:
          action: perform-action
          perform_action: esphome.broadlink_rm4_pro_rf_learn
      - type: button
        name: Cancel
        icon: mdi:close-circle-outline
        tap_action:
          action: perform-action
          perform_action: esphome.broadlink_rm4_pro_rf_cancel
```

Together with the automation under [Learning a remote](#learning-a-remote), a
tap on **Learn a remote** guides through both steps with notifications.

## HAIR and the Broadlink integration

- **HAIR** is for infrared only: it talks to Home Assistant's `infrared` platform
  and its Sniffer captures IR, so it cannot learn an RF code. RF devices are an
  [open request](https://github.com/DAB-LABS/HAIR/issues/4). Use `rf_learn` and
  your own switches and buttons for RF.
- The **Broadlink integration** learns and sends RF through its `remote` entity
  while the RM4 Pro runs the Broadlink firmware (`remote.learn_command` with
  `command_type: rf`, also in two steps: hold, then press briefly). With this
  firmware the equivalents are `rf_learn` and `rf_send_raw_at`:

| Broadlink integration | This firmware |
|---|---|
| `remote.learn_command`, `command_type: rf` | `rf_learn` |
| Hold the key (the device sweeps the frequencies) | phase `sweep`, then `found` |
| Press the key briefly | phase `capture`, then `learned` |
| The code is stored in `.storage` under the device and the command | You paste it into a switch, button or script |
| `remote.send_command` | `rf_send_raw_at` |
| Radio frequency platform (transmitter only) | `RF Transmitter` entity |

Codes learned with the Broadlink app or integration are a compressed binary
packet and cannot be imported; learn them again.

## When something does not work

- **`RF Signal Strength` stays unknown, the log says `No reply to command 0x60E`**:
  the module does not answer. Look for `nothing arrived on the UART` or `bytes
  arrived`; see [Troubleshooting](troubleshooting.md).
- **Learning finds no frequency**: hold the key closer and longer, or lower
  `sweep_margin`, see [Troubleshooting](troubleshooting.md).
- **Events fire for noise**: raise `min_pulses`.
- **The choice does not follow `rf_frequency`**: the last choice wins over the
  config after the first start; pick the frequency in Home Assistant.
- **The ESPHome build says it needs 2026.10**: the radio needs it, or use
  `pro-ir.yaml`.
