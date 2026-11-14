import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CURRENT,
    CONF_ENERGY,
    CONF_FLOW,
    CONF_HEAD,
    CONF_POWER,
    CONF_SPEED,
    CONF_TIME_ID,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_VOLUME_FLOW_RATE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_CUBIC_METER_PER_HOUR,
    UNIT_HOUR,
    UNIT_KILOWATT_HOURS,
    UNIT_METER,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
)
import esphome.final_validate as fv
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha

DEPENDENCIES = ["grundfos_alpha"]

CONF_ALARM_CODE = "alarm_code"
CONF_CLOCK_DEVIATION = "clock_deviation"
CONF_MEDIUM_TEMPERATURE = "medium_temperature"
CONF_OPERATING_HOURS = "operating_hours"
CONF_STARTS = "starts"
CONF_WARNING_CODE = "warning_code"

ICON_FAULT_CODE = "mdi:alert-circle-outline"


def _schema(unit: str, decimals: int, device_class: str | None = None) -> cv.Schema:
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        accuracy_decimals=decimals,
        device_class=device_class or cv.UNDEFINED,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        cv.Optional(CONF_FLOW): _schema(
            UNIT_CUBIC_METER_PER_HOUR, 3, DEVICE_CLASS_VOLUME_FLOW_RATE
        ),
        cv.Optional(CONF_HEAD): _schema(UNIT_METER, 2),
        cv.Optional(CONF_MEDIUM_TEMPERATURE): _schema(
            UNIT_CELSIUS, 1, DEVICE_CLASS_TEMPERATURE
        ),
        cv.Optional(CONF_VOLTAGE): _schema(UNIT_VOLT, 1, DEVICE_CLASS_VOLTAGE),
        cv.Optional(CONF_CURRENT): _schema(UNIT_AMPERE, 3, DEVICE_CLASS_CURRENT),
        cv.Optional(CONF_POWER): _schema(UNIT_WATT, 1, DEVICE_CLASS_POWER),
        cv.Optional(CONF_SPEED): _schema(UNIT_REVOLUTIONS_PER_MINUTE, 0),
        # The energy, operating hours and starts are held over the small fall after a power cut (see publish_)
        cv.Optional(CONF_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_OPERATING_HOURS): sensor.sensor_schema(
            unit_of_measurement=UNIT_HOUR,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_DURATION,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_STARTS): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Grundfos fault codes, 0 = no fault
        cv.Optional(CONF_WARNING_CODE): sensor.sensor_schema(
            icon=ICON_FAULT_CODE,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ALARM_CODE): sensor.sensor_schema(
            icon=ICON_FAULT_CODE,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # The pump's clock minus the time source, found at each check before the clock is set
        cv.Optional(CONF_CLOCK_DEVIATION): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            icon="mdi:clock-alert-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


def _final_validate(config: ConfigType) -> ConfigType:
    if CONF_CLOCK_DEVIATION not in config:
        return config
    full_config = fv.full_config.get()
    hub_path = full_config.get_path_for_id(config[CONF_GRUNDFOS_ALPHA_ID])[:-1]
    if CONF_TIME_ID not in full_config.get_config_for_path(hub_path):
        raise cv.Invalid(
            f"{CONF_CLOCK_DEVIATION} needs {CONF_TIME_ID} in the grundfos_alpha hub",
            path=[CONF_CLOCK_DEVIATION],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    sensors = sensor.sub_sensors(config)
    await sensors(CONF_FLOW, hub.set_flow_sensor)
    await sensors(CONF_HEAD, hub.set_head_sensor)
    await sensors(CONF_MEDIUM_TEMPERATURE, hub.set_medium_temperature_sensor)
    await sensors(CONF_VOLTAGE, hub.set_voltage_sensor)
    await sensors(CONF_CURRENT, hub.set_current_sensor)
    await sensors(CONF_POWER, hub.set_power_sensor)
    await sensors(CONF_SPEED, hub.set_speed_sensor)
    await sensors(CONF_ENERGY, hub.set_energy_sensor)
    await sensors(CONF_OPERATING_HOURS, hub.set_operating_hours_sensor)
    await sensors(CONF_STARTS, hub.set_starts_sensor)
    await sensors(CONF_WARNING_CODE, hub.set_warning_code_sensor)
    await sensors(CONF_ALARM_CODE, hub.set_alarm_code_sensor)
    await sensors(CONF_CLOCK_DEVIATION, hub.set_clock_deviation_sensor)
