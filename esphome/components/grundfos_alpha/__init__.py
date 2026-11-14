import esphome.codegen as cg
from esphome.components import genibus, time
import esphome.config_validation as cv
from esphome.const import CONF_TIME_ID, CONF_UPDATE_INTERVAL
from esphome.types import ConfigType

AUTO_LOAD = ["genibus"]
CODEOWNERS = ["@tomwellnitz"]
DOMAIN = "grundfos_alpha"
MULTI_CONF = True

CONF_GRUNDFOS_ALPHA_ID = "grundfos_alpha_id"
CONF_SET_CLOCK = "set_clock"

grundfos_alpha_ns = cg.esphome_ns.namespace("grundfos_alpha")
GrundfosAlpha = grundfos_alpha_ns.class_("GrundfosAlpha", genibus.GenibusDevice)

# One poll with all entities is about a dozen requests, and a request without an answer takes the bus's 1 s
# timeout and 250 ms quiet period. An update is skipped while the last poll still runs, so a shorter interval only
# keeps the pump's Bluetooth link busy.
MIN_UPDATE_INTERVAL = cv.TimePeriod(seconds=5)
validate_update_interval = cv.All(cv.update_interval, cv.Range(min=MIN_UPDATE_INTERVAL))


def _validate_set_clock(config: ConfigType) -> ConfigType:
    if config[CONF_SET_CLOCK] and CONF_TIME_ID not in config:
        raise cv.Invalid(
            f"{CONF_SET_CLOCK} needs {CONF_TIME_ID}", path=[CONF_SET_CLOCK]
        )
    return config


# 0xE7 is the address the ALPHA pumps answer to over Bluetooth
CONFIG_SCHEMA = cv.All(
    genibus.genibus_device_schema(0xE7)
    .extend({cv.GenerateID(): cv.declare_id(GrundfosAlpha)})
    .extend(cv.polling_component_schema("30s"))
    .extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): validate_update_interval,
            # Compares the pump's clock with the local time after the first poll of each connection and once a day.
            # The pump keeps local time, so the time source needs the right timezone.
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # Also sets the pump's clock when it is 2 s or more off; its clock program acts at that time
            cv.Optional(CONF_SET_CLOCK, default=False): cv.boolean,
        }
    ),
    _validate_set_clock,
)


async def to_code(config: ConfigType) -> None:
    var = await genibus.new_genibus_device(config)
    if (time_id := config.get(CONF_TIME_ID)) is not None:
        cg.add_define("USE_GRUNDFOS_ALPHA_CLOCK")
        cg.add(var.set_time_id(await cg.get_variable(time_id)))
        if config[CONF_SET_CLOCK]:
            cg.add(var.set_clock_writes(True))
