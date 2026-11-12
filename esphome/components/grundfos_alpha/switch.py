import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import (
    CONF_INITIAL_STATE,
    CONF_RESTORE_MODE,
    CONF_RESTORE_STATE,
    ENTITY_CATEGORY_CONFIG,
)
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha

DEPENDENCIES = ["grundfos_alpha"]

CONF_AUTOMATIC_NIGHT_SETBACK = "automatic_night_setback"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        cv.Optional(CONF_AUTOMATIC_NIGHT_SETBACK): switch.switch_schema(
            switch.ApplySwitch,
            default_restore_mode="DISABLED",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:weather-night",
        ).extend(
            {
                # The pump keeps the setting, and nothing is written before the pump is connected
                cv.Optional(key): cv.invalid(
                    "Not supported: the switch shows the setting read from the pump"
                )
                for key in (CONF_RESTORE_MODE, CONF_RESTORE_STATE, CONF_INITIAL_STATE)
            }
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    switches = switch.sub_switches(config, parent=hub)
    # The hub publishes the state it reads back after the write
    await switches(
        CONF_AUTOMATIC_NIGHT_SETBACK,
        hub.set_automatic_night_setback_switch,
        apply="write_automatic_night_setback(state)",
    )
