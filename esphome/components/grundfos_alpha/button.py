import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_STOP
from esphome.types import ConfigType

from . import CONF_GRUNDFOS_ALPHA_ID, GrundfosAlpha, grundfos_alpha_ns

DEPENDENCIES = ["grundfos_alpha"]

CONF_RESET_ALARM = "reset_alarm"
CONF_START = "start"

Command = grundfos_alpha_ns.enum("Command", is_class=True)

# The class 3 commands the Grundfos GO app sends to an ALPHA3. Whether a stop lasts through a loss of power is not
# tested yet.
# key: icon
_BUTTONS = {
    CONF_START: "mdi:play",
    CONF_STOP: "mdi:stop",
    CONF_RESET_ALARM: "mdi:alarm-off",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_GRUNDFOS_ALPHA_ID): cv.use_id(GrundfosAlpha),
        **{
            cv.Optional(key): button.button_schema(button.ApplyButton, icon=icon)
            for key, icon in _BUTTONS.items()
        },
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_GRUNDFOS_ALPHA_ID])
    cg.add_define("USE_GRUNDFOS_ALPHA_SETTINGS")
    cg.add_define("USE_GRUNDFOS_ALPHA_WRITE")
    buttons = button.sub_buttons(config, parent=hub)
    # The hub keeps them only for its config dump
    await buttons(
        CONF_START,
        hub.set_start_button,
        apply=f"send_command({Command.COMMAND_START})",
    )
    await buttons(
        CONF_STOP, hub.set_stop_button, apply=f"send_command({Command.COMMAND_STOP})"
    )
    await buttons(
        CONF_RESET_ALARM,
        hub.set_reset_alarm_button,
        apply=f"send_command({Command.COMMAND_RESET_ALARM})",
    )
