"""Draft: a read-only sensor for one value of a GENIbus class 10 data object.

The schema is a proposal to be agreed with the maintainers before it is submitted.
"""

import esphome.codegen as cg
from esphome.components import genibus, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_MULTIPLY, CONF_OFFSET, CONF_TYPE
from esphome.types import ConfigType

CONF_EXPECTED_TYPE = "expected_type"
CONF_EXPECTED_VERSION = "expected_version"
CONF_OBJECT = "object"
CONF_SUB_ID = "sub_id"

GenibusSensor = genibus.genibus_ns.class_(
    "GenibusSensor", sensor.Sensor, genibus.GenibusDevice
)
ValueType = genibus.genibus_ns.enum("ValueType", is_class=True)

# Value type -> enum value and size in bytes; all values are big-endian
VALUE_TYPES = {
    "uint8": (ValueType.VALUE_TYPE_UINT8, 1),
    "uint16": (ValueType.VALUE_TYPE_UINT16, 2),
    "uint32": (ValueType.VALUE_TYPE_UINT32, 4),
    "int8": (ValueType.VALUE_TYPE_INT8, 1),
    "int16": (ValueType.VALUE_TYPE_INT16, 2),
    "int32": (ValueType.VALUE_TYPE_INT32, 4),
    "float": (ValueType.VALUE_TYPE_FLOAT, 4),
    "double": (ValueType.VALUE_TYPE_DOUBLE, 8),
}

# A reply APDU holds at most 63 data bytes, 7 of them the object header
MAX_BODY_SIZE = 63 - 7


def _validate_offset(config: ConfigType) -> ConfigType:
    size = VALUE_TYPES[config[CONF_TYPE]][1]
    if config[CONF_OFFSET] + size > MAX_BODY_SIZE:
        raise cv.Invalid(
            f"A {config[CONF_TYPE]} at offset {config[CONF_OFFSET]} ends past the "
            f"largest object body of {MAX_BODY_SIZE} bytes",
            path=[CONF_OFFSET],
        )
    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(GenibusSensor)
    .extend(genibus.genibus_device_schema(0))
    .extend(cv.polling_component_schema("60s"))
    .extend(
        {
            # A generic sensor knows no default unit address
            cv.Required(CONF_ADDRESS): cv.hex_uint8_t,
            cv.Required(CONF_OBJECT): cv.uint8_t,
            cv.Required(CONF_SUB_ID): cv.uint16_t,
            cv.Required(CONF_OFFSET): cv.uint8_t,
            cv.Required(CONF_TYPE): cv.one_of(*VALUE_TYPES, lower=True),
            cv.Optional(CONF_EXPECTED_TYPE): cv.hex_uint16_t,
            cv.Optional(CONF_EXPECTED_VERSION): cv.uint8_t,
            cv.Optional(CONF_MULTIPLY): cv.float_,
        }
    ),
    _validate_offset,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_OBJECT],
        config[CONF_SUB_ID],
        config[CONF_OFFSET],
        VALUE_TYPES[config[CONF_TYPE]][0],
    )
    await sensor.register_sensor(var, config)
    await genibus.register_genibus_device(var, config)
    cg.add_optional(var.set_expected_type, config.get(CONF_EXPECTED_TYPE))
    cg.add_optional(var.set_expected_version, config.get(CONF_EXPECTED_VERSION))
    cg.add_optional(var.set_multiply, config.get(CONF_MULTIPLY))
