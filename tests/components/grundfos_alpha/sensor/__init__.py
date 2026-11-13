import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    async def to_code_testing(config: ConfigType) -> None:
        # The hub emits it with time_id; the gtests set the time source themselves
        cg.add_define("USE_GRUNDFOS_ALPHA_CLOCK")

    manifest.to_code = to_code_testing
    manifest.dependencies = manifest.dependencies + ["time"]
