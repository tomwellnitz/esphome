"""Tests for the grundfos_alpha select options."""

from pathlib import Path
import re

import pytest

from esphome.components.grundfos_alpha import select

CPP = Path(select.__file__).with_name("grundfos_alpha.cpp")


def _cpp_table(name: str) -> list[int]:
    match = re.search(
        rf"static constexpr uint8_t {name}\[\] = \{{([^}}]*)\}};", CPP.read_text()
    )
    assert match is not None, name
    return [int(value) for value in match.group(1).split(",")]


@pytest.mark.parametrize(
    ("table", "options"),
    [
        ("CONTROL_MODES", select.CONTROL_MODE_OPTIONS),
        ("OPERATING_MODES", select.OPERATING_MODE_OPTIONS),
    ],
)
def test_options_follow_the_values_in_the_cpp_table(
    table: str, options: dict[int, str]
) -> None:
    # The hub maps an option index to the value at that index of the table
    assert _cpp_table(table) == list(options)
