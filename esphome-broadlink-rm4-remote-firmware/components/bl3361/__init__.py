"""BL3361 433 MHz module of the Broadlink RM4 Pro (EXPERIMENTAL).

The module is tuned over a UART and passes the radio signal through one raw
data pin. See protocol.h for where the knowledge about the module comes from.
"""

from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import number, select, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CODE,
    CONF_FILTER,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_OPTIONS,
    CONF_UPDATE_INTERVAL,
    STATE_CLASS_MEASUREMENT,
)

CODEOWNERS = ["@seb5594"]
AUTO_LOAD = ["sensor", "select", "number"]

CONF_BL3361_ID = "bl3361_id"
CONF_DATA_PIN = "data_pin"
CONF_IDLE = "idle"
CONF_MIN_PULSES = "min_pulses"
CONF_LOG_FRAMES = "log_frames"
CONF_RSSI = "rssi"
CONF_ON_RECEIVE = "on_receive"
CONF_REPEAT = "repeat"
CONF_GAP = "gap"
CONF_SETTLE_TIME = "settle_time"
CONF_SWEEP_MARGIN = "sweep_margin"
CONF_FREQUENCY_SELECT = "frequency_select"
CONF_FREQUENCY_NUMBER = "frequency_number"
CONF_RESTORE_VALUE = "restore_value"
CONF_ON_LEARN = "on_learn"
CONF_APPLY = "apply"

# Tuning ranges of the module in kHz, see radio_config.h.
SUPPORTED_BANDS_KHZ = ((305000, 339999), (430000, 439999))

bl3361_ns = cg.esphome_ns.namespace("bl3361")
BL3361Component = bl3361_ns.class_("BL3361Component", cg.Component)

FrequencyNumber = bl3361_ns.class_(
    "FrequencyNumber", number.Number, cg.Parented.template(BL3361Component)
)
FrequencySelect = bl3361_ns.class_(
    "FrequencySelect", select.Select, cg.Parented.template(BL3361Component)
)

StartReceiveAction = bl3361_ns.class_("StartReceiveAction", automation.Action)
FindFrequencyAction = bl3361_ns.class_("FindFrequencyAction", automation.Action)
LearnAction = bl3361_ns.class_("LearnAction", automation.Action)
CancelAction = bl3361_ns.class_("CancelAction", automation.Action)
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


def frequency_option_name(hertz):
    """How an option of the frequency entity is called: 433.92 MHz."""
    return f"{hertz / 1e6:g} MHz"


def validate_frequency_options(value):
    """A list of frequencies for the select entity, without duplicates."""
    options = cv.ensure_list(validate_frequency)(value)
    if len(set(options)) != len(options):
        raise cv.Invalid("Every frequency can only be in the list once")
    if not options:
        raise cv.Invalid("The list needs at least one frequency")
    return options


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
            # Frames in the first moments after tuning are noise of the
            # receiver and not dropped by length alone.
            cv.Optional(
                CONF_SETTLE_TIME, default="1500ms"
            ): cv.positive_time_period_milliseconds,
            # How far the strongest channel has to stand out of the average of
            # all channels when searching a frequency (relative signal strength).
            cv.Optional(CONF_SWEEP_MARGIN, default=3): cv.int_range(min=1, max=20),
            # The frequency of the radio as a choice in Home Assistant.
            cv.Optional(CONF_FREQUENCY_SELECT): select.select_schema(
                FrequencySelect,
                entity_category="config",
                icon="mdi:sine-wave",
            ).extend(
                {
                    cv.Optional(
                        CONF_OPTIONS, default=["315MHz", "433.92MHz"]
                    ): validate_frequency_options,
                    # Remember the last frequency over a restart.
                    cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
                }
            ),
            # Any frequency in MHz, for one that is not in the list.
            cv.Optional(CONF_FREQUENCY_NUMBER): number.number_schema(
                FrequencyNumber,
                entity_category="config",
                icon="mdi:sine-wave",
                unit_of_measurement="MHz",
            ).extend({cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean}),
            # The trigger belongs to the component, so there is exactly one
            # list of actions.
            cv.Optional(CONF_ON_RECEIVE): automation.validate_automation(single=True),
            # Progress of a frequency search or a learning session.
            cv.Optional(CONF_ON_LEARN): automation.validate_automation(single=True),
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
    cg.add(var.set_settle_time(config[CONF_SETTLE_TIME]))
    cg.add(var.set_sweep_margin(config[CONF_SWEEP_MARGIN]))

    if CONF_FREQUENCY_SELECT in config:
        conf = config[CONF_FREQUENCY_SELECT]
        options = conf[CONF_OPTIONS]
        # The last option, "Custom", shows a frequency that is not in the list.
        names = [frequency_option_name(hertz) for hertz in options] + ["Custom"]
        sel = await select.new_select(conf, options=names)
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(sel.set_options_khz([round(hertz / 1000) for hertz in options]))
        cg.add(var.set_frequency_select(sel, conf[CONF_RESTORE_VALUE]))

    if CONF_RSSI in config:
        rssi = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(rssi))
        cg.add(var.set_rssi_interval(config[CONF_RSSI][CONF_UPDATE_INTERVAL]))

    if CONF_FREQUENCY_NUMBER in config:
        conf = config[CONF_FREQUENCY_NUMBER]
        # The band between 340 and 430 MHz is refused when it is entered.
        num = await number.new_number(
            conf, min_value=305.0, max_value=440.0, step=0.01
        )
        await cg.register_parented(num, config[CONF_ID])
        cg.add(var.set_frequency_number(num, conf[CONF_RESTORE_VALUE]))

    if CONF_ON_LEARN in config:
        await automation.build_automation(
            var.get_learn_trigger(),
            [
                (cg.std_string, "phase"),
                (cg.float_, "frequency_mhz"),
                (cg.uint32, "repeats"),
                (cg.std_vector.template(cg.int32), "code"),
            ],
            config[CONF_ON_LEARN],
        )

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
    "bl3361.find_frequency",
    FindFrequencyAction,
    ACTION_BASE_SCHEMA.extend(
        {cv.Optional(CONF_APPLY, default=True): cv.templatable(cv.boolean)}
    ),
    synchronous=True,
)
async def find_frequency_to_code(config, action_id, template_arg, args):
    var = await register_simple_action(config, action_id, template_arg, args)
    cg.add(var.set_apply(await cg.templatable(config[CONF_APPLY], args, bool)))
    return var


@automation.register_action(
    "bl3361.cancel", CancelAction, ACTION_BASE_SCHEMA, synchronous=True
)
async def cancel_to_code(config, action_id, template_arg, args):
    return await register_simple_action(config, action_id, template_arg, args)


@automation.register_action(
    "bl3361.learn", LearnAction, ACTION_BASE_SCHEMA, synchronous=True
)
async def learn_to_code(config, action_id, template_arg, args):
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
            # Transmit on this frequency instead of the configured one, for this
            # code only. Without it the current frequency is used.
            cv.Optional(CONF_FREQUENCY): cv.templatable(validate_frequency),
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
    if CONF_FREQUENCY in config:
        cg.add(
            var.set_frequency_hz(
                await cg.templatable(config[CONF_FREQUENCY], args, cg.uint32)
            )
        )
    return var
