import esphome.codegen as cg
from esphome.components import text_sensor
from esphome.components.const import CONF_OPERATING_MODE
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha

DEPENDENCIES = ["grundfos_alpha"]

CONF_BLUETOOTH_SOFTWARE_VERSION = "bluetooth_software_version"
CONF_CONTROL_SOURCE = "control_source"
CONF_LAST_EVENT = "last_event"
CONF_PRODUCT_NAME = "product_name"
CONF_PRODUCT_NUMBER = "product_number"
CONF_SOFTWARE_VERSION = "software_version"

# Read once per connection
PRODUCT_TEXTS: dict[str, str] = {
    CONF_PRODUCT_NAME: "mdi:pump",
    CONF_PRODUCT_NUMBER: "mdi:barcode",
    CONF_SOFTWARE_VERSION: "mdi:chip",
    CONF_BLUETOOTH_SOFTWARE_VERSION: "mdi:bluetooth",
}

# The operating mode and control source in effect (object 86/8), the product texts (class 7) and the latest entry of
# the event log (object 88)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        cv.Optional(CONF_OPERATING_MODE): text_sensor.text_sensor_schema(
            icon="mdi:pump"
        ),
        cv.Optional(CONF_CONTROL_SOURCE): text_sensor.text_sensor_schema(
            icon="mdi:remote"
        ),
        **{
            cv.Optional(key): text_sensor.text_sensor_schema(
                icon=icon, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            )
            for key, icon in PRODUCT_TEXTS.items()
        },
        cv.Optional(CONF_LAST_EVENT): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    text_sensors = text_sensor.sub_text_sensors(config)
    await text_sensors(CONF_OPERATING_MODE, hub.set_operating_mode_text_sensor)
    await text_sensors(CONF_CONTROL_SOURCE, hub.set_control_source_text_sensor)
    await text_sensors(CONF_PRODUCT_NAME, hub.set_product_name_text_sensor)
    await text_sensors(CONF_PRODUCT_NUMBER, hub.set_product_number_text_sensor)
    await text_sensors(CONF_SOFTWARE_VERSION, hub.set_software_version_text_sensor)
    await text_sensors(
        CONF_BLUETOOTH_SOFTWARE_VERSION, hub.set_bluetooth_software_version_text_sensor
    )
    await text_sensors(CONF_LAST_EVENT, hub.set_last_event_text_sensor)
