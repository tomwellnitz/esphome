import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.const import CONF_OPERATING_MODE
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha

DEPENDENCIES = ["grundfos_alpha"]

CONF_CONTROL_SOURCE = "control_source"

# The operating mode and control source in effect (object 86/8)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        cv.Optional(CONF_OPERATING_MODE): text_sensor.text_sensor_schema(
            icon="mdi:pump"
        ),
        cv.Optional(CONF_CONTROL_SOURCE): text_sensor.text_sensor_schema(
            icon="mdi:remote"
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    text_sensors = text_sensor.sub_text_sensors(config)
    await text_sensors(CONF_OPERATING_MODE, hub.set_operating_mode_text_sensor)
    await text_sensors(CONF_CONTROL_SOURCE, hub.set_control_source_text_sensor)
