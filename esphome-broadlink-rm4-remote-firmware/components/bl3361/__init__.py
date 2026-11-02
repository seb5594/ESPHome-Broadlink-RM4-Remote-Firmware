"""BL3361 433 MHz module of the Broadlink RM4 Pro (EXPERIMENTAL).

The module is tuned over a UART and passes the radio signal through one raw
data pin. See protocol.h for where the knowledge about the module comes from.
"""

from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CODE,
    CONF_FILTER,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    STATE_CLASS_MEASUREMENT,
)

CODEOWNERS = ["@seb5594"]
AUTO_LOAD = ["sensor"]

CONF_BL3361_ID = "bl3361_id"
CONF_DATA_PIN = "data_pin"
CONF_IDLE = "idle"
CONF_MIN_PULSES = "min_pulses"
CONF_LOG_FRAMES = "log_frames"
CONF_RSSI = "rssi"
CONF_ON_RECEIVE = "on_receive"
CONF_REPEAT = "repeat"
CONF_GAP = "gap"

# Tuning ranges of the module in kHz, see radio_config.h.
SUPPORTED_BANDS_KHZ = ((305000, 339999), (430000, 439999))

bl3361_ns = cg.esphome_ns.namespace("bl3361")
BL3361Component = bl3361_ns.class_("BL3361Component", cg.Component)

StartReceiveAction = bl3361_ns.class_("StartReceiveAction", automation.Action)
StopAction = bl3361_ns.class_("StopAction", automation.Action)
ScanAction = bl3361_ns.class_("ScanAction", automation.Action)
SetFrequencyAction = bl3361_ns.class_("SetFrequencyAction", automation.Action)
SendRawAction = bl3361_ns.class_("SendRawAction", automation.Action)


def validate_frequency(value):
    """Accepts anything like 433.92MHz and checks that the module can tune to it."""
    hertz = cv.frequency(value)
    khz = round(hertz / 1000)
    if not any(first <= khz <= last for first, last in SUPPORTED_BANDS_KHZ):
        raise cv.Invalid(
            "The RF module tunes to 305-340 MHz and 430-440 MHz only, "
            f"{hertz / 1e6:.3f} MHz is outside"
        )
    return hertz


def validate_code(value):
    """A raw code is a list of signed microsecond durations (+ on, - off)."""
    pulses = cv.ensure_list(cv.int_)(value)
    if not 1 <= len(pulses) <= 2048:
        raise cv.Invalid("A code needs between 1 and 2048 pulses")
    if any(pulse == 0 for pulse in pulses):
        raise cv.Invalid("A pulse of 0 microseconds is not possible")
    return pulses


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BL3361Component),
            # Declared as an input; the component switches the pin to an output
            # while it transmits, which the pin validation knows nothing about.
            cv.Required(CONF_DATA_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_FREQUENCY, default="433.92MHz"): validate_frequency,
            # Pulses shorter than this are noise of the receiver and get merged.
            cv.Optional(
                CONF_FILTER, default="100us"
            ): cv.positive_time_period_microseconds,
            # A pause this long ends a received frame.
            cv.Optional(
                CONF_IDLE, default="15ms"
            ): cv.positive_time_period_microseconds,
            # Frames with fewer pulses are thrown away as noise.
            cv.Optional(CONF_MIN_PULSES, default=24): cv.int_range(min=2, max=500),
            cv.Optional(CONF_LOG_FRAMES, default=True): cv.boolean,
            cv.Optional(CONF_RSSI): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:signal",
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend(
                {
                    cv.Optional(
                        CONF_UPDATE_INTERVAL, default="1s"
                    ): cv.positive_time_period_milliseconds,
                }
            ),
            # The trigger belongs to the component, so there is exactly one
            # list of actions.
            cv.Optional(CONF_ON_RECEIVE): automation.validate_automation(single=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    # The serial link uses LibreTiny's Serial0, see module_serial.h.
    cv.only_on(["rtl87xx"]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_data_pin(await cg.gpio_pin_expression(config[CONF_DATA_PIN])))
    cg.add(var.set_frequency_khz(round(config[CONF_FREQUENCY] / 1000)))
    cg.add(
        var.set_pulse_limits(
            config[CONF_FILTER], config[CONF_IDLE], config[CONF_MIN_PULSES]
        )
    )
    cg.add(var.set_log_frames(config[CONF_LOG_FRAMES]))

    if CONF_RSSI in config:
        rssi = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(rssi))
        cg.add(var.set_rssi_interval(config[CONF_RSSI][CONF_UPDATE_INTERVAL]))

    if CONF_ON_RECEIVE in config:
        await automation.build_automation(
            var.get_receive_trigger(),
            [(cg.std_vector.template(cg.int32), "x")],
            config[CONF_ON_RECEIVE],
        )


ACTION_BASE_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(BL3361Component)})


async def register_simple_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "bl3361.start_receive", StartReceiveAction, ACTION_BASE_SCHEMA, synchronous=True
)
async def start_receive_to_code(config, action_id, template_arg, args):
    return await register_simple_action(config, action_id, template_arg, args)


@automation.register_action(
    "bl3361.stop", StopAction, ACTION_BASE_SCHEMA, synchronous=True
)
async def stop_to_code(config, action_id, template_arg, args):
    return await register_simple_action(config, action_id, template_arg, args)


@automation.register_action(
    "bl3361.scan", ScanAction, ACTION_BASE_SCHEMA, synchronous=True
)
async def scan_to_code(config, action_id, template_arg, args):
    return await register_simple_action(config, action_id, template_arg, args)


@automation.register_action(
    "bl3361.set_frequency",
    SetFrequencyAction,
    ACTION_BASE_SCHEMA.extend(
        {cv.Required(CONF_FREQUENCY): cv.templatable(validate_frequency)}
    ),
    synchronous=True,
)
async def set_frequency_to_code(config, action_id, template_arg, args):
    var = await register_simple_action(config, action_id, template_arg, args)
    template_ = await cg.templatable(config[CONF_FREQUENCY], args, cg.uint32)
    cg.add(var.set_frequency_hz(template_))
    return var


@automation.register_action(
    "bl3361.send_raw",
    SendRawAction,
    ACTION_BASE_SCHEMA.extend(
        {
            cv.Required(CONF_CODE): cv.templatable(validate_code),
            cv.Optional(CONF_REPEAT, default=5): cv.templatable(
                cv.int_range(min=1, max=30)
            ),
            cv.Optional(CONF_GAP, default="10ms"): cv.templatable(
                cv.positive_time_period_microseconds
            ),
        }
    ),
    synchronous=True,
)
async def send_raw_to_code(config, action_id, template_arg, args):
    var = await register_simple_action(config, action_id, template_arg, args)
    code = config[CONF_CODE]
    if cg.is_template(code):
        template_ = await cg.templatable(code, args, cg.std_vector.template(cg.int32))
        cg.add(var.set_code_template(template_))
    else:
        cg.add(var.set_code_static(code))
    cg.add(var.set_repeat(await cg.templatable(config[CONF_REPEAT], args, cg.uint32)))
    cg.add(var.set_gap_us(await cg.templatable(config[CONF_GAP], args, cg.uint32)))
    return var
