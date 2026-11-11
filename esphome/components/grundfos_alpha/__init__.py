import esphome.codegen as cg
from esphome.components import genibus
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

AUTO_LOAD = ["genibus"]
CODEOWNERS = ["@tomwellnitz"]
DOMAIN = "grundfos_alpha"
MULTI_CONF = True

CONF_GRUNDFOS_ALPHA_ID = "grundfos_alpha_id"

grundfos_alpha_ns = cg.esphome_ns.namespace("grundfos_alpha")
GrundfosAlpha = grundfos_alpha_ns.class_("GrundfosAlpha", genibus.GenibusDevice)

# 0xE7 is the address the ALPHA pumps answer to over Bluetooth
CONFIG_SCHEMA = (
    genibus.genibus_device_schema(0xE7)
    .extend({cv.GenerateID(): cv.declare_id(GrundfosAlpha)})
    .extend(cv.polling_component_schema("30s"))
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await genibus.register_genibus_device(var, config)
