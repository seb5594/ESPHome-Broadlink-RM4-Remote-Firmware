"""The RF module as a native Home Assistant `radio_frequency` entity.

    radio_frequency:
      - platform: bl3361
        name: RF Transmitter
        frequency: 434.3MHz   # optional, see below

The entity sends, and it reports what the module receives (the same
transmissions as `on_receive`). Without `frequency` it declares the tuning range
of the module, 305-340 MHz and 430-440 MHz. A receiver that declares one
frequency tells Home Assistant (and tools like HAIR) which carrier the codes it
hears are on, so name the frequency of your remotes there.

Needs ESPHome 2026.10 or newer: the radio_frequency component is experimental
and its interface changed in that release.
"""

import esphome.codegen as cg
from esphome.components import radio_frequency
import esphome.config_validation as cv
from esphome.const import CONF_FREQUENCY, __version__ as ESPHOME_VERSION

from . import BL3361Component, CONF_BL3361_ID, bl3361_ns, validate_frequency

CODEOWNERS = ["@seb5594"]
DEPENDENCIES = ["bl3361"]

RadioFrequencyEntity = bl3361_ns.class_(
    "RadioFrequencyEntity",
    radio_frequency.RadioFrequency,
    cg.Parented.template(BL3361Component),
)

MINIMUM_ESPHOME = (2026, 10)


def validate_esphome_version(value):
    """The interface this platform is written for exists since 2026.10."""
    numbers = ESPHOME_VERSION.split("-")[0].split(".")
    try:
        version = tuple(int(part) for part in numbers[:2])
    except ValueError:
        return value  # a development build with an unusual name, let it try
    if version < MINIMUM_ESPHOME:
        raise cv.Invalid(
            "The radio_frequency entity of the BL3361 needs ESPHome 2026.10 or "
            f"newer, this is {ESPHOME_VERSION}"
        )
    return value


CONFIG_SCHEMA = cv.All(
    radio_frequency.radio_frequency_schema(RadioFrequencyEntity).extend(
        {
            cv.GenerateID(CONF_BL3361_ID): cv.use_id(BL3361Component),
            cv.Optional(CONF_FREQUENCY): validate_frequency,
        }
    ),
    validate_esphome_version,
)


async def to_code(config):
    var = await radio_frequency.new_radio_frequency(config)
    await cg.register_parented(var, config[CONF_BL3361_ID])
    if CONF_FREQUENCY in config:
        cg.add(var.set_fixed_frequency_hz(round(config[CONF_FREQUENCY])))
