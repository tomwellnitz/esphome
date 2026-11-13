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

CONF_CLOCK_PROGRAM = "clock_program"
CONF_PUMP_KICK = "pump_kick"


def _switch_schema(icon: str) -> cv.Schema:
    return switch.switch_schema(
        switch.ApplySwitch,
        block_inverted=True,
        default_restore_mode="DISABLED",
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=icon,
    ).extend(
        {
            # The pump keeps the setting, and nothing is written before the pump is connected
            cv.Optional(key): cv.invalid(
                "Not supported: the switch shows the setting read from the pump"
            )
            for key in (CONF_RESTORE_MODE, CONF_RESTORE_STATE, CONF_INITIAL_STATE)
        }
    )


# The pump kick runs a pump that stood still for a while (86/2). The clock program (84/1) switches all weekly
# schedule and single events kept in the pump; they are set up in the Grundfos GO app and not offered here. Their
# actions can stop the pump: action 1 is a stop from the factory.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        cv.Optional(CONF_PUMP_KICK): _switch_schema("mdi:refresh-auto"),
        cv.Optional(CONF_CLOCK_PROGRAM): _switch_schema("mdi:calendar-clock"),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    switches = switch.sub_switches(config, parent=hub)
    # The hub publishes the state it reads back after the write
    await switches(
        CONF_PUMP_KICK, hub.set_pump_kick_switch, apply="write_pump_kick(state)"
    )
    await switches(
        CONF_CLOCK_PROGRAM,
        hub.set_clock_program_switch,
        apply="write_clock_program(state)",
    )
