import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

CODEOWNERS = ["@tomwellnitz"]
DOMAIN = "genibus"

CONF_GENIBUS_ID = "genibus_id"
CONF_MASTER_ADDRESS = "master_address"

genibus_ns = cg.esphome_ns.namespace("genibus")
GenibusBus = genibus_ns.class_("GenibusBus", cg.Component)
GenibusDevice = genibus_ns.class_(
    "GenibusDevice", cg.PollingComponent, cg.Parented.template(GenibusBus)
)

# Base schema of a transport (BLE, RS-485) that derives from GenibusBus
GENIBUS_BUS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MASTER_ADDRESS, default=0xF8): cv.hex_uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)

# Base schema of a device on the bus
GENIBUS_DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GENIBUS_ID): cv.use_id(GenibusBus),
        cv.Optional(CONF_ADDRESS, default=0xE7): cv.hex_uint8_t,
    }
)

_register_device = cg.slot_list("GENIBUS_DEVICE_COUNT", "register_device", DOMAIN)


async def register_genibus_bus(var: MockObj, config: ConfigType) -> None:
    """Register a transport created with GENIBUS_BUS_SCHEMA."""
    await cg.register_component(var, config)
    cg.add(var.set_master_address(config[CONF_MASTER_ADDRESS]))


async def register_genibus_device(var: MockObj, config: ConfigType) -> None:
    """Register a device created with GENIBUS_DEVICE_SCHEMA, and give it a request slot on its bus."""
    await cg.register_component(var, config)
    bus = await cg.get_variable(config[CONF_GENIBUS_ID])
    await cg.register_parented(var, bus)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(_register_device(bus, var))
