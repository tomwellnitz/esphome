"""Tests for the grundfos_alpha config validation."""

import pytest

from esphome import config_validation as cv, final_validate as fv
from esphome.components.genibus import CONF_GENIBUS_ID
from esphome.components.grundfos_alpha import (
    CONF_GRUNDFOS_ALPHA_ID,
    CONF_SET_CLOCK,
    CONFIG_SCHEMA,
    _validate_set_clock,
    validate_update_interval,
)
from esphome.components.grundfos_alpha.sensor import (
    FINAL_VALIDATE_SCHEMA as sensor_final_validate,
)
from esphome.components.grundfos_alpha.switch import (
    CONF_AUTOMATIC_NIGHT_SETBACK,
    CONFIG_SCHEMA as SWITCH_SCHEMA,
)
from esphome.config import Config
from esphome.const import (
    CONF_ID,
    CONF_INITIAL_STATE,
    CONF_RESTORE_MODE,
    CONF_RESTORE_STATE,
    CONF_TIME_ID,
    CONF_UPDATE_INTERVAL,
    PlatformFramework,
)
from esphome.core import ID
from esphome.types import ConfigType
from tests.component_tests.types import SetCoreConfigCallable


@pytest.mark.parametrize("value", ["5s", "30s", "10min"])
def test_update_interval_of_5_s_or_more_is_accepted(value: str) -> None:
    assert validate_update_interval(value) == cv.update_interval(value)


def test_update_interval_never_is_accepted() -> None:
    assert validate_update_interval("never") == cv.update_interval("never")


@pytest.mark.parametrize("value", ["4s", "1s", "500ms"])
def test_update_interval_below_5_s_is_rejected(value: str) -> None:
    with pytest.raises(cv.Invalid):
        validate_update_interval(value)


@pytest.mark.parametrize(
    ("config", "expected"),
    [({}, 30), ({CONF_UPDATE_INTERVAL: "5s"}, 5)],
)
def test_schema_defaults_to_30_s_and_accepts_5_s(
    set_core_config: SetCoreConfigCallable, config: ConfigType, expected: int
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)
    result = CONFIG_SCHEMA({CONF_GENIBUS_ID: "bus", **config})
    assert result[CONF_UPDATE_INTERVAL] == cv.TimePeriod(seconds=expected)


def test_schema_rejects_an_update_interval_below_5_s(
    set_core_config: SetCoreConfigCallable,
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)
    with pytest.raises(cv.Invalid, match="at least 5s"):
        CONFIG_SCHEMA({CONF_GENIBUS_ID: "bus", CONF_UPDATE_INTERVAL: "4s"})


def test_set_clock_is_off_by_default() -> None:
    schema = CONFIG_SCHEMA.validators[0].schema
    keys = [k for k in schema if k == CONF_SET_CLOCK]
    assert len(keys) == 1
    assert keys[0].default() is False


def test_set_clock_needs_the_time_id() -> None:
    with pytest.raises(cv.Invalid, match="time_id"):
        _validate_set_clock({CONF_SET_CLOCK: True})


@pytest.mark.parametrize(
    "config",
    [{CONF_SET_CLOCK: False}, {CONF_SET_CLOCK: True, CONF_TIME_ID: ID("clock")}],
)
def test_set_clock_with_the_time_id_or_off_is_accepted(config: dict) -> None:
    assert _validate_set_clock(config) is config


@pytest.mark.parametrize(
    ("key", "value"),
    [
        (CONF_RESTORE_MODE, "RESTORE_DEFAULT_OFF"),
        (CONF_RESTORE_MODE, "DISABLED"),
        (CONF_RESTORE_STATE, "previous"),
        (CONF_INITIAL_STATE, True),
    ],
)
def test_switch_rejects_restore_options(key: str, value: object) -> None:
    schema = SWITCH_SCHEMA.schema[CONF_AUTOMATIC_NIGHT_SETBACK]
    with pytest.raises(cv.Invalid, match="Not supported"):
        schema({"name": "Night setback", key: value})


def test_switch_does_not_restore() -> None:
    schema = SWITCH_SCHEMA.schema[CONF_AUTOMATIC_NIGHT_SETBACK]
    config = schema({"name": "Night setback"})
    assert str(config[CONF_RESTORE_MODE]) == "DISABLED"


def _set_hub(hub: dict) -> None:
    full_config = Config()
    full_config.update({"grundfos_alpha": [hub]})
    full_config.declare_ids.append((hub[CONF_ID], ["grundfos_alpha", 0, CONF_ID]))
    fv.full_config.set(full_config)


def _sensors(**extra: object) -> dict:
    return {CONF_GRUNDFOS_ALPHA_ID: ID("pump"), **extra}


def test_clock_deviation_needs_the_time_id() -> None:
    _set_hub({CONF_ID: ID("pump", is_declaration=True)})
    with pytest.raises(cv.Invalid, match="time_id"):
        sensor_final_validate(_sensors(clock_deviation={}))


def test_clock_deviation_with_the_time_id_is_accepted() -> None:
    _set_hub({CONF_ID: ID("pump", is_declaration=True), CONF_TIME_ID: ID("clock")})
    config = _sensors(clock_deviation={})
    assert sensor_final_validate(config) is config


def test_sensors_without_clock_deviation_need_no_time_id() -> None:
    _set_hub({CONF_ID: ID("pump", is_declaration=True)})
    config = _sensors(flow={})
    assert sensor_final_validate(config) is config
