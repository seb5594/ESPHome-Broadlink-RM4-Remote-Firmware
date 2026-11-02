# Home Assistant

After the first boot, Home Assistant shows a new ESPHome device under
**Settings → Devices & services**. Click **Add**, enter the API encryption key
from your secrets, and the device brings these entities (names depend on your
`friendly_name`):

| Entity | Example ID (RM4C Mini) | What it does |
|---|---|---|
| IR Emitter | `infrared.broadlink_rm4c_mini_ir_emitter` | Sends IR codes. Its state is the time of the last transmission. |
| IR Receiver | `infrared.broadlink_rm4c_mini_ir_receiver` | Receives IR codes. Its state is the time of the last received code. |
| Button | `binary_sensor.broadlink_rm4c_mini_button` | The physical button. |

An RM4 Pro with the experimental 433 MHz radio has more: the frequency as a
choice and as a number, a diagnostic **RF Signal Strength** sensor, the native
**RF Transmitter** entity, the events `esphome.<name>_rf_received` and
`esphome.<name>_rf_learn` and the actions `rf_learn`, `rf_send_raw`,
`rf_send_raw_at`, `rf_set_frequency` and `rf_scan`. They are described in
[Using the radio in Home Assistant](rf-home-assistant.md).

Current Home Assistant releases list all emitters and receivers under
**Settings → Connectivity → Infrared**. Integrations for IR-controlled devices, such as
[LG Infrared](https://www.home-assistant.io/integrations/lg_infrared/) or
[Samsung Infrared](https://www.home-assistant.io/integrations/samsung_infrared/),
ask for an emitter during setup; pick the RM4 there.

## Using it with HAIR

[HAIR](https://github.com/DAB-LABS/HAIR) is a custom integration with its own
sidebar panel: capture codes from any remote, turn them into devices with
buttons, media players, climate entities and more, and send them through any
emitter. It talks to the native `infrared` platform only, so it works with
this firmware without any extra ESPHome configuration.

### Setup

1. Install HAIR through HACS and restart Home Assistant:
   [![Open HAIR in HACS](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=DAB-LABS&repository=HAIR&category=integration)
2. **Settings → Devices & services → Add integration → HAIR**. The setup finds
   the RM4's emitter and receiver on its own.
3. Open **HAIR** in the sidebar. On the **Devices** tab the RM4 appears with a
   `TX-NATIVE` emitter and an `RX-NATIVE` receiver.

### Learning a remote

1. Open the **Sniffer** tab, point your remote at the RM4 and press a few
   buttons (a new remote needs about three presses before HAIR lists it).
2. Click **USE** → **Make a Device**, pick the RM4 as emitter.
3. HAIR creates a device with one button entity per captured code. Map
   commands to actions (power, volume, …) to get a matching media player,
   climate or fan entity.

For air conditioners, set `ir_receiver_idle: 80ms` first, otherwise one press
may show up as several signals.

### Moving codes over from the Broadlink integration

Codes you learned with `remote.learn_command` while the RM4 still ran the
Broadlink firmware don't have to be learned again. The Broadlink integration
keeps them in `.storage/broadlink_remote_<mac>_codes`, and HAIR's **Plucker**
reads these files (read-only) and turns them into HAIR devices that send
through the RM4's new emitter. The files stay in place when the Broadlink
integration is removed, but make a backup of `.storage` first anyway.

Codes learned in the Broadlink app live in Broadlink's cloud and can't be
imported this way.

Automations need one small change afterwards: HAIR gives every device its own
`remote` entity, so the `device:` parameter goes away.

```yaml
# before (Broadlink integration)
action: remote.send_command
target:
  entity_id: remote.rm4c_mini
data:
  device: tv
  command: power

# after (HAIR device "TV")
action: remote.send_command
target:
  entity_id: remote.tv   # the remote entity HAIR created for the device
data:
  command: power
```
