"""Tests for the grundfos_alpha config validation."""

import pytest

from esphome import config_validation as cv
from esphome.components.grundfos_alpha import CONFIG_SCHEMA, validate_update_interval
from esphome.components.grundfos_alpha.switch import (
    CONF_CLOCK_PROGRAM,
    CONF_PUMP_KICK,
    CONFIG_SCHEMA as SWITCH_SCHEMA,
)
from esphome.const import (
    CONF_INITIAL_STATE,
    CONF_RESTORE_MODE,
    CONF_RESTORE_STATE,
    CONF_UPDATE_INTERVAL,
)


@pytest.mark.parametrize("value", ["5s", "30s", "10min"])
def test_update_interval_of_5_s_or_more_is_accepted(value: str) -> None:
    assert validate_update_interval(value) == cv.update_interval(value)


def test_update_interval_never_is_accepted() -> None:
    assert validate_update_interval("never") == cv.update_interval("never")


@pytest.mark.parametrize("value", ["4s", "1s", "500ms"])
def test_update_interval_below_5_s_is_rejected(value: str) -> None:
    with pytest.raises(cv.Invalid):
        validate_update_interval(value)


def test_schema_uses_the_checked_update_interval() -> None:
    keys = [k for k in CONFIG_SCHEMA.schema if k == CONF_UPDATE_INTERVAL]
    assert len(keys) == 1
    assert keys[0].default() == "30s"
    assert CONFIG_SCHEMA.schema[keys[0]] is validate_update_interval


@pytest.mark.parametrize("switch", [CONF_PUMP_KICK, CONF_CLOCK_PROGRAM])
@pytest.mark.parametrize(
    ("key", "value"),
    [
        (CONF_RESTORE_MODE, "RESTORE_DEFAULT_OFF"),
        (CONF_RESTORE_MODE, "DISABLED"),
        (CONF_RESTORE_STATE, "previous"),
        (CONF_INITIAL_STATE, True),
    ],
)
def test_switch_rejects_restore_options(switch: str, key: str, value: object) -> None:
    schema = SWITCH_SCHEMA.schema[switch]
    with pytest.raises(cv.Invalid, match="Not supported"):
        schema({"name": "Pump", key: value})


@pytest.mark.parametrize("switch", [CONF_PUMP_KICK, CONF_CLOCK_PROGRAM])
def test_switch_does_not_restore(switch: str) -> None:
    config = SWITCH_SCHEMA.schema[switch]({"name": "Pump"})
    assert str(config[CONF_RESTORE_MODE]) == "DISABLED"


@pytest.mark.parametrize("switch", [CONF_PUMP_KICK, CONF_CLOCK_PROGRAM])
def test_switch_rejects_inverted(switch: str) -> None:
    with pytest.raises(cv.Invalid):
        SWITCH_SCHEMA.schema[switch]({"name": "Pump", "inverted": True})
