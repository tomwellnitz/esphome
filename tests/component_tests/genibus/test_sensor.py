"""Config-validation tests for the genibus sensor platform."""

import pytest
from voluptuous import Invalid

from esphome.components.genibus.sensor import CONFIG_SCHEMA
from esphome.const import PlatformFramework
from esphome.types import ConfigType
from tests.component_tests.types import SetCoreConfigCallable


@pytest.fixture(autouse=True)
def _core(set_core_config: SetCoreConfigCallable) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)


def _validate(**extra: object) -> ConfigType:
    return CONFIG_SCHEMA(
        {
            "name": "value",
            "address": 0xE7,
            "object": 87,
            "sub_id": 1,
            "offset": 0,
            "type": "float",
            **extra,
        }
    )


def test_last_bytes_of_largest_body_accepted() -> None:
    """A double may end at the last byte of a 56-byte body."""
    config = _validate(offset=48, type="double")
    assert config["offset"] == 48


@pytest.mark.parametrize(("offset", "value_type"), [(49, "double"), (55, "uint16")])
def test_value_past_largest_body_rejected(offset: int, value_type: str) -> None:
    """A value that ends past the largest body a reply can carry is rejected."""
    with pytest.raises(Invalid, match="ends past the largest object body"):
        _validate(offset=offset, type=value_type)


def test_address_required() -> None:
    """A generic sensor has no default unit address."""
    config = {
        "name": "value",
        "object": 87,
        "sub_id": 1,
        "offset": 0,
        "type": "float",
    }
    with pytest.raises(Invalid, match="address"):
        CONFIG_SCHEMA(config)


def test_unknown_type_rejected() -> None:
    """Only the listed value types are accepted."""
    with pytest.raises(Invalid):
        _validate(type="int64")
