from functools import partial

import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    ENTITY_CATEGORY_CONFIG,
    UNIT_HOUR,
    UNIT_METER,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_SECOND,
)
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha, grundfos_alpha_ns

DEPENDENCIES = ["grundfos_alpha"]

CONF_CONSTANT_PRESSURE_SETPOINT = "constant_pressure_setpoint"
CONF_CONSTANT_SPEED_SETPOINT = "constant_speed_setpoint"
CONF_PROPORTIONAL_PRESSURE_SETPOINT = "proportional_pressure_setpoint"
CONF_PUMP_KICK_DURATION = "pump_kick_duration"
CONF_PUMP_KICK_WAIT = "pump_kick_wait"

NumberSetting = grundfos_alpha_ns.enum("NumberSetting", is_class=True)

# The ranges cover all ALPHA3 sizes (data booklet 99392358: 1 to 6 m head); the hub also checks the limits the
# pump reports, and writes a set point only while the pump is in its control mode. Grundfos gives no speed range in
# rpm; the app data shows 1900 to 6000 rpm for a XX-60. One real ALPHA3 reported 1.5 to 4.5 m for constant pressure,
# 2 to 4.5 m for proportional pressure and 1900 to 5300 rpm.
# key: (setting, icon, unit, min, max, step)
_NUMBERS = {
    CONF_CONSTANT_PRESSURE_SETPOINT: (
        NumberSetting.NUMBER_SETTING_CONSTANT_PRESSURE,
        "mdi:gauge",
        UNIT_METER,
        1.0,
        6.0,
        0.1,
    ),
    CONF_PROPORTIONAL_PRESSURE_SETPOINT: (
        NumberSetting.NUMBER_SETTING_PROPORTIONAL_PRESSURE,
        "mdi:gauge",
        UNIT_METER,
        1.0,
        6.0,
        0.1,
    ),
    CONF_CONSTANT_SPEED_SETPOINT: (
        NumberSetting.NUMBER_SETTING_CONSTANT_SPEED,
        "mdi:speedometer",
        UNIT_REVOLUTIONS_PER_MINUTE,
        1000,
        6000,
        10,
    ),
    # The pump kick (86/2): a real ALPHA3 came with 25 h without running and a 120 s run, and took 60 s. The pump
    # reports no limits; the ranges are a choice, and the read-back shows whether the pump kept the value.
    CONF_PUMP_KICK_WAIT: (
        NumberSetting.NUMBER_SETTING_PUMP_KICK_WAIT,
        "mdi:timer-sand",
        UNIT_HOUR,
        1,
        168,
        1,
    ),
    CONF_PUMP_KICK_DURATION: (
        NumberSetting.NUMBER_SETTING_PUMP_KICK_DURATION,
        "mdi:timer-outline",
        UNIT_SECOND,
        10,
        600,
        10,
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        **{
            cv.Optional(key): number.number_schema(
                number.ApplyNumber,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon=icon,
                unit_of_measurement=unit,
            )
            for key, (_, icon, unit, *_) in _NUMBERS.items()
        },
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    numbers = number.sub_numbers(config, parent=hub)
    # The hub publishes the value it reads back after the write
    for key, (setting, _, _, min_value, max_value, step) in _NUMBERS.items():
        await numbers(
            key,
            partial(hub.set_number, setting),
            apply=f"write_number({setting}, value)",
            min_value=min_value,
            max_value=max_value,
            step=step,
        )
