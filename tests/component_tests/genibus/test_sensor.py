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
            "expected_type": 0x00E8,
            "expected_version": 1,
            **extra,
        }
    )


def test_last_bytes_of_largest_body_accepted() -> None:
    """A double may end at the last of the 46 body bytes an ALPHA3 sends."""
    config = _validate(offset=38, type="double")
    assert config["offset"] == 38


@pytest.mark.parametrize(("offset", "value_type"), [(39, "double"), (45, "uint16")])
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
        "expected_type": 0x00E8,
        "expected_version": 1,
    }
    with pytest.raises(Invalid, match="address"):
        CONFIG_SCHEMA(config)


@pytest.mark.parametrize("key", ["expected_type", "expected_version"])
def test_expected_type_and_version_required(key: str) -> None:
    """A reply names no object, so type and version must be given."""
    config = {
        "name": "value",
        "address": 0xE7,
        "object": 87,
        "sub_id": 1,
        "offset": 0,
        "type": "float",
        "expected_type": 0x00E8,
        "expected_version": 1,
    }
    del config[key]
    with pytest.raises(Invalid, match=key):
        CONFIG_SCHEMA(config)


def test_multiply_not_accepted() -> None:
    """Scaling is left to the sensor filters."""
    with pytest.raises(Invalid):
        _validate(multiply=2.0)


def test_unknown_type_rejected() -> None:
    """Only the listed value types are accepted."""
    with pytest.raises(Invalid):
        _validate(type="int64")
