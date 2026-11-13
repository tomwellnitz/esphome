from functools import partial

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

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha, grundfos_alpha_ns

DEPENDENCIES = ["grundfos_alpha"]

CONF_CLOCK_PROGRAM = "clock_program"
CONF_PUMP_KICK = "pump_kick"

SwitchSetting = grundfos_alpha_ns.enum("SwitchSetting", is_class=True)

# The pump kick runs a pump that stood still for a while (86/2). The clock program (84/1) switches the weekly
# schedule and the single events kept in the pump; they are set up in the Grundfos GO app, and their actions can
# stop the pump.
# key: (setting, icon)
_SWITCHES = {
    CONF_PUMP_KICK: (SwitchSetting.SWITCH_SETTING_PUMP_KICK, "mdi:refresh-auto"),
    CONF_CLOCK_PROGRAM: (
        SwitchSetting.SWITCH_SETTING_CLOCK_PROGRAM,
        "mdi:calendar-clock",
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        **{
            cv.Optional(key): switch.switch_schema(
                switch.ApplySwitch,
                default_restore_mode="DISABLED",
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=icon,
            ).extend(
                {
                    # The pump keeps the setting, and nothing is written before the pump is connected
                    cv.Optional(option): cv.invalid(
                        "Not supported: the switch shows the setting read from the pump"
                    )
                    for option in (
                        CONF_RESTORE_MODE,
                        CONF_RESTORE_STATE,
                        CONF_INITIAL_STATE,
                    )
                }
            )
            for key, (_, icon) in _SWITCHES.items()
        },
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    switches = switch.sub_switches(config, parent=hub)
    # The hub publishes the state it reads back after the write
    for key, (setting, _) in _SWITCHES.items():
        await switches(
            key,
            partial(hub.set_switch, setting),
            apply=f"write_switch({setting}, state)",
        )
