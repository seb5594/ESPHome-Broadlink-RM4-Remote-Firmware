# Adding a model

The firmware is built from small parts, so another Broadlink is mostly a new file
that says which parts it has and where its pins are. This page shows how the
parts fit together and what to do for a new model.

- [How the parts fit](#how-the-parts-fit)
- [The pins a feature needs](#the-pins-a-feature-needs)
- [A new model in six steps](#a-new-model-in-six-steps)
- [A feature a model may leave out](#a-feature-a-model-may-leave-out)
- [A new feature or another chip](#a-new-feature-or-another-chip)

## How the parts fit

```text
esphome-broadlink-rm4-remote-firmware/
  devices/rm4/mini.yaml       a model: identity, pin map, list of features
  devices/rm4/pro.yaml
  features/ir/ir.yaml         a feature: one function, with the YAML it needs
  features/rf/bl3361.yaml
  features/controls/controls.yaml
  common/base.yaml            what every device has: API, OTA, hotspot
  platform/rtl8720c/          what every device with this chip has
  components/bl3361/          code that ESPHome does not bring (the radio)
```

A **model file** is the only place that knows about one piece of hardware. It
sets the pins as substitutions and includes the parts. A **feature** knows
nothing about a model: it reads its pins from the substitutions and works on any
hardware that has the function. ESPHome merges all of it into one configuration,
the user's own file on top.

## The pins a feature needs

Every feature file names the substitutions it expects in its first lines.

| Feature | Substitutions | Notes |
|---|---|---|
| `ir/ir.yaml` | `pin_ir_tx`, `pin_ir_rx` | `ir_receiver_idle` is optional (25 ms). |
| `controls/controls.yaml` | `pin_status_led`, `pin_button` | LED and button are active low. |
| `rf/bl3361.yaml` | `pin_rf_data`, `name` | Needs the BL3361 module; the UART to it is fixed. |

A pin that a feature needs and the model leaves out shows up as a warning that
`${pin_ir_tx}` could not be resolved because `pin_ir_tx` is undefined, followed by
the failed validation; the message names the pin and the feature file.

## A new model in six steps

1. **Copy** [`devices/rm4/mini.yaml`](../esphome-broadlink-rm4-remote-firmware/devices/rm4/mini.yaml)
   to a new file in `devices/<family>/`, for example `devices/rm4/<model>.yaml`.
2. **Set the identity**: `name`, `friendly_name` and `esphome.project.name`
   (`seb5594.broadlink-<model>`).
3. **Set the pins**. Take them from a boot log of the stock firmware or from a
   flash dump, and write down where they came from in the header of the file, as
   [`pro.yaml`](../esphome-broadlink-rm4-remote-firmware/devices/rm4/pro.yaml) does.
4. **List the features** the model has under `packages:`. A model without an
   infrared receiver leaves out `ir`, a model without a button leaves out
   `controls`, and so on. If the chip is another one, the `platform` line points
   at that chip's folder (see below).
5. **Add an example config** like [`rm4-mini.example.yaml`](../rm4-mini.example.yaml)
   for the users, with the path of the new model file.
6. **Add it to the CI**: a config `.github/tests/ci-<model>.yaml` like
   [`ci-rm4-mini.yaml`](../.github/tests/ci-rm4-mini.yaml), and a line for it in
   the `esphome` matrix and in the `remote-package` matrix of
   [`validate.yml`](../.github/workflows/validate.yml). Then a page in
   `docs/devices/` and a row in the table of the [README](../README.md).

## A feature a model may leave out

A feature that not every owner of a model wants is chosen by a substitution that
names its file. [`pro.yaml`](../esphome-broadlink-rm4-remote-firmware/devices/rm4/pro.yaml)
does it for the radio:

```yaml
substitutions:
  rf_driver: bl3361

packages:
  rf: !include ../../features/rf/${rf_driver}.yaml
```

`rf_driver: none` in the user's config includes
[`features/rf/none.yaml`](../esphome-broadlink-rm4-remote-firmware/features/rf/none.yaml),
which is empty. Constraints that only the feature has, such as the ESPHome
version of the radio, live in the feature file, so leaving it out leaves them out.

## A new feature or another chip

- **A new feature** is a folder in `features/` with one YAML file. Write the
  substitutions it expects at the top, give everything that is not a pin a default
  under `substitutions:`, and keep model knowledge out of it. If it needs code
  ESPHome does not have, put an external component in `components/`, with host
  tests like the ones of `bl3361` for the parts that do not need the hardware.
- **Another chip** gets its own folder next to
  [`platform/rtl8720c/`](../esphome-broadlink-rm4-remote-firmware/platform/rtl8720c/)
  with the board, the framework and the logger settings; the model includes that
  instead. The features stay as they are as long as ESPHome offers the same
  platforms on that chip.
