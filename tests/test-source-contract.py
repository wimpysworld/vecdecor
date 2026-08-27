#!/usr/bin/env python3

import re
import sys
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[1] / "src"
if len(sys.argv) > 1 and not sys.argv[-1].startswith("-"):
    requested_root = Path(sys.argv.pop())
    SOURCE_ROOT = (
        requested_root / "src"
        if (requested_root / "src").is_dir()
        else requested_root
    )

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}

REMOVED_FILES = {
    "deco-effects.cpp",
    "deco-effects.hpp",
    "smoke-shaders.hpp",
}

REMOVED_OPTIONS = {
    "csd_titlebar_height",
    "overlay_engine",
    "effect_type",
    "effect_color",
    "animate",
    "maximized_shadows",
    "shadow_radius",
    "shadow_color",
}

REMOVED_EFFECTS = {
    "smoke",
    "ink",
    "clouds",
    "halftone",
    "pattern",
    "lava",
    "hex",
    "zebra",
    "neural_network",
    "hexagon_maze",
    "raymarched_truchet",
    "neon_pattern",
    "neon_rings",
}

REMOVED_IDENTIFIERS = {
    "DECORATION_AREA_SHADOW",
    "GL_COMPUTE_SHADER",
    "advect1_program",
    "advect2_program",
    "beveled_glass",
    "beveled_glass_overlay",
    "create_programs",
    "create_textures",
    "diffuse1_program",
    "diffuse2_program",
    "destroy_programs",
    "destroy_textures",
    "effect_color",
    "effect_animate",
    "effect_type",
    "effect_updated",
    "hook_set",
    "maximized_shadows",
    "motion_program",
    "neural_network_tex",
    "option_changed_cb",
    "overlay_engine",
    "project1_program",
    "project2_program",
    "project3_program",
    "project4_program",
    "project5_program",
    "project6_program",
    "recreate_textures",
    "render_effect",
    "render_overlay_program",
    "run_shader",
    "run_shader_region",
    "shadow_color",
    "shadow_radius",
    "shadow_thickness",
    "smoke_t",
    "setup_shader",
    "stitch_smoke_shader",
    "step_effect",
}

ALLOWED_SHADOW_IDENTIFIERS = {
    "get_shadow_margin",
    "wf_shadow_margin_t",
    "winshadows",
}

CPP_NON_CODE_PATTERN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
CPP_IDENTIFIER_PATTERN = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

REMOVED_FLUID_TEXTURES = {
    "b0u",
    "b0v",
    "b0d",
    "b1u",
    "b1v",
    "b1d",
}

RETAINED_IDENTIFIERS = {
    "deco-button.hpp": {
        "button_state_model_t state_model",
    },
    "deco-button-state.hpp": {
        "button_state_model_t",
        "request_button_textures",
    },
    "deco-layout.hpp": {
        "layout_input_model_t input_model",
    },
    "deco-layout-model.hpp": {
        "DOUBLE_CLICK_TIMEOUT_MS = 300",
        "layout_input_model_t",
    },
    "shade-state.hpp": {
        "shade_state_model_t",
        "shade_frame_plan_t",
    },
    "deco-background-renderer.hpp": {
        "background_renderer_t",
        "corner_radius",
    },
    "deco-theme.hpp": {
        "background_renderer_t background_renderer",
        'rounded_corner_radius{"vecdecor/rounded_corner_radius"}',
    },
    "shade.hpp": {
        "shade_state_model_t state",
    },
}


class SourceContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_files = sorted(
            path
            for path in SOURCE_ROOT.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
        cls.sources = {
            path: path.read_text(encoding="utf-8") for path in cls.source_files
        }

    def locations_with_pattern(self, pattern):
        matches = []
        for path, source in self.sources.items():
            if re.search(pattern, source):
                matches.append(str(path.relative_to(SOURCE_ROOT)))
        return matches

    def forbidden_shadow_identifiers(self):
        matches = []
        for path, source in self.sources.items():
            code = CPP_NON_CODE_PATTERN.sub(" ", source)
            for identifier in CPP_IDENTIFIER_PATTERN.findall(code):
                components = re.sub(
                    r"(?<=[a-z0-9])(?=[A-Z])", "_", identifier
                ).lower().split("_")
                if (
                    any(component in {"shadow", "shadows"} for component in components)
                    and identifier.lower() not in ALLOWED_SHADOW_IDENTIFIERS
                ):
                    matches.append(
                        f"{path.relative_to(SOURCE_ROOT)}:{identifier}"
                    )
        return matches

    def test_removed_source_files_stay_removed(self):
        present = {
            path.name for path in SOURCE_ROOT.rglob("*") if path.is_file()
        }
        self.assertFalse(
            present & REMOVED_FILES,
            "removed effect source files returned",
        )

    def test_removed_public_option_selectors_stay_removed(self):
        for option in sorted(REMOVED_OPTIONS):
            with self.subTest(option=option):
                pattern = re.escape(f'vecdecor/{option}')
                self.assertFalse(
                    self.locations_with_pattern(pattern),
                    f"removed public option selector vecdecor/{option} returned",
                )

    def test_removed_effect_selectors_and_shaders_stay_removed(self):
        for effect in sorted(REMOVED_EFFECTS):
            with self.subTest(effect=effect):
                patterns = (
                    rf'["\']{re.escape(effect)}["\']',
                    rf'\brender_source_{re.escape(effect)}\b',
                )
                locations = {
                    location
                    for pattern in patterns
                    for location in self.locations_with_pattern(pattern)
                }
                self.assertFalse(
                    locations,
                    f"removed effect selector or shader {effect} returned",
                )

        for selector in ("deco", "rounded_corners", "beveled_glass"):
            with self.subTest(selector=selector):
                pattern = rf'["\']{re.escape(selector)}["\']'
                self.assertFalse(
                    self.locations_with_pattern(pattern),
                    f"removed effect selector {selector} returned",
                )

    def test_removed_effect_runtime_stays_removed(self):
        for identifier in sorted(REMOVED_IDENTIFIERS | REMOVED_FLUID_TEXTURES):
            with self.subTest(identifier=identifier):
                pattern = rf'\b{re.escape(identifier)}\b'
                self.assertFalse(
                    self.locations_with_pattern(pattern),
                    f"removed effect runtime identifier {identifier} returned",
                )

    def test_builtin_shadow_runtime_stays_removed(self):
        self.assertFalse(
            self.forbidden_shadow_identifiers(),
            "built-in shadow drawing or configuration returned",
        )

    def test_retained_production_contracts_remain_present(self):
        for filename, identifiers in RETAINED_IDENTIFIERS.items():
            source = (SOURCE_ROOT / filename).read_text(encoding="utf-8")
            for identifier in sorted(identifiers):
                with self.subTest(filename=filename, identifier=identifier):
                    self.assertIn(
                        identifier,
                        source,
                        f"retained production identifier {identifier} is missing",
                    )

    def test_wlroots_xcursor_header_keeps_c_linkage(self):
        source = self.sources[SOURCE_ROOT / "deco-layout.cpp"]
        include = "#include <wlr/xcursor.h>"
        self.assertEqual(
            source.count(include),
            1,
            "deco-layout.cpp must include wlr/xcursor.h exactly once",
        )
        self.assertIn(
            f'extern "C"\n{{\n{include}\n}}',
            source,
            "wlr/xcursor.h must retain C linkage",
        )

    def test_csd_shade_height_uses_base_geometry_and_live_updates(self):
        source = self.sources[SOURCE_ROOT / "decoration.cpp"]
        self.assertIn(
            "pixdecor_theme_t::get_base_title_height()",
            source,
            "CSD Shade must use the resolved base title height",
        )
        self.assertIn(
            "get_shade_titlebar_height(toplevel)",
            source,
            "CSD Shade must resolve the shared title height",
        )
        self.assertIn(
            "update_csd_shade_titlebar_heights();",
            source,
            "CSD title height changes must update CSD Shade",
        )
        self.assertIn(
            "tr->set_titlebar_height(titlebar_height);",
            source,
            "live CSD height updates must set the titlebar height",
        )

        shade_source = self.sources[SOURCE_ROOT / "shade.hpp"]
        self.assertIn(
            "production.request_refresh();",
            shade_source,
            "live title height updates must request a margin and rendering refresh",
        )


if __name__ == "__main__":
    unittest.main()
