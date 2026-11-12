from functools import partial

import esphome.codegen as cg
from esphome.components import select
from esphome.components.const import CONF_OPERATING_MODE
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha, grundfos_alpha_ns

DEPENDENCIES = ["grundfos_alpha"]

CONF_CONTROL_MODE = "control_mode"

SelectSetting = grundfos_alpha_ns.enum("SelectSetting", is_class=True)

# The options follow CONTROL_MODES and OPERATING_MODES in grundfos_alpha.cpp, the values the Grundfos GO app offers.
# Whether Stop lasts through a loss of power is not tested yet.
# key: (setting, icon, options)
_SELECTS = {
    CONF_CONTROL_MODE: (
        SelectSetting.SELECT_SETTING_CONTROL_MODE,
        "mdi:tune-variant",
        [
            "Constant pressure",
            "Proportional pressure",
            "Constant curve",
            "Radiator (AUTOADAPT)",
            "Underfloor heating (AUTOADAPT)",
            "Radiator and underfloor heating (AUTOADAPT)",
        ],
    ),
    CONF_OPERATING_MODE: (
        SelectSetting.SELECT_SETTING_OPERATING_MODE,
        "mdi:pump",
        ["Normal", "Stop", "Min", "Max"],
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        **{
            cv.Optional(key): select.select_schema(
                select.ApplySelect,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=icon,
            )
            for key, (_, icon, _) in _SELECTS.items()
        },
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    selects = select.sub_selects(config, parent=hub)
    # The hub publishes the option it reads back after the write
    for key, (setting, _, options) in _SELECTS.items():
        await selects(
            key,
            partial(hub.set_select, setting),
            apply=f"write_select({setting}, index)",
            options=options,
        )
