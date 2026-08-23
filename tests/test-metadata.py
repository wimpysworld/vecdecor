#!/usr/bin/env python3

import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ASSET_DIRECTORY = Path(sys.argv.pop())
METADATA_PATH = Path(sys.argv.pop())


class MetadataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = ET.parse(METADATA_PATH).getroot()

    def option(self, name):
        option = self.root.find(f".//option[@name='{name}']")
        self.assertIsNotNone(option, f"missing {name} metadata option")
        return option

    def assert_size_option(self, name, group_name):
        option = self.option(name)
        self.assertEqual(option.get("type"), "int")
        self.assertEqual(option.findtext("default"), "0")
        self.assertEqual(option.findtext("min"), "0")

        parent = next(
            group for group in self.root.findall(".//group") if option in list(group)
        )
        self.assertEqual(parent.findtext("_short"), group_name)

        minimum = int(option.findtext("min"))
        self.assertLess(-1, minimum, "metadata must reject negative values")

    def test_button_size_contract(self):
        self.assert_size_option("button_size", "Buttons")

    def test_title_height_contract(self):
        self.assert_size_option("title_height", "General")

    def test_button_colour_defaults(self):
        expected = {
            "button_color": "0.803922 0.839216 0.956863 1.0",
            "button_inactive_color": "0.529412 0.533333 0.572549 1.0",
            "button_hover_color": "0.192157 0.196078 0.266667 1.0",
            "button_pressed_color": "0.270588 0.278431 0.352941 1.0",
        }
        for name, value in expected.items():
            with self.subTest(name=name):
                self.assertEqual(self.option(name).findtext("default"), value)

    def test_button_asset_geometry(self):
        namespace = "{http://www.w3.org/2000/svg}"
        glyphs = {
            "minimize": [("rect", {
                "x": "4", "y": "8", "width": "8", "height": "1",
                "rx": "0.5", "ry": "0.5",
            })],
            "maximize": [("path", {"d": "M 6,4 C 4.892,4 4,4.892 4,6 v 4 c 0,1.108 0.89201,2 2,2 h 4 c 1.10801,0 2,-0.892 2,-2 V 6 C 12,4.892 11.10799,4 10,4 Z m 0,1 h 4 c 0.554,0 1,0.44602 1,1 v 4 c 0,0.55398 -0.44602,1 -1,1 H 6 C 5.44603,11 5.00001,10.55398 5.00001,10 V 6 C 5.00001,5.44602 5.44604,5 6,5 Z"})],
            "restore": [
                ("path", {"d": "M 6,6 C 4.892,6 4,6.892 4,8 v 2 c 0,1.108 0.892,2 2,2 h 2 c 1.108,0 2,-0.892 2,-2 V 8 C 10,6.892 9.108,6 8,6 Z m 0,1 h 2 c 0.554,0 1,0.446 1,1 v 2 c 0,0.554 -0.446,1 -1,1 H 6 C 5.446,11 5,10.554 5,10 V 8 C 5,7.446 5.446,7 6,7 Z"}),
                ("path", {"d": "M 8,4 C 6.892,4 6,4.892 6,6 H 7 C 7,5.446 7.446,5 8,5 h 2 c 0.554,0 1,0.446 1,1 v 2 c 0,0.554 -0.446,1 -1,1 v 1 c 1.108,0 2,-0.892 2,-2 V 6 C 12,4.892 11.108,4 10,4 Z", "opacity": "0.35"}),
            ],
            "close": [("path", {"d": "m 4.464745,3.96488 c -0.12775,0 -0.2555,0.0486 -0.35339,0.14649 -0.19578,0.19586 -0.19578,0.51116 0,0.70703 L 7.292955,8 l -3.1816,3.1816 c -0.19578,0.19586 -0.19578,0.51116 0,0.70703 0.19578,0.19586 0.51118,0.19586 0.70704,0 l 3.18161,-3.1816 3.1816,3.1816 c 0.19578,0.19586 0.51114,0.19586 0.70704,0 0.19578,-0.19586 0.19578,-0.51116 0,-0.70703 L 8.707045,8 l 3.1816,-3.1816 c 0.19578,-0.19586 0.19578,-0.51116 0,-0.70703 -0.19578,-0.19586 -0.51117,-0.19586 -0.70704,0 l -3.1816,3.1816 -3.18161,-3.1816 C 4.720495,4.01347 4.592755,3.96488 4.465005,3.96488 Z"})],
        }
        active_circle = {
            "minimize": "#f9e2af",
            "maximize": "#a6e3a1",
            "restore": "#a6e3a1",
            "close": "#f38ba8",
        }
        expected_files = {
            f"{control}{suffix}.svg"
            for control in glyphs
            for suffix in ("", "-hover", "-inactive", "-inactive-hover")
        }
        self.assertEqual(
            {path.name for path in ASSET_DIRECTORY.glob("*.svg")}, expected_files
        )
        for filename in sorted(expected_files):
            with self.subTest(filename=filename):
                root = ET.parse(ASSET_DIRECTORY / filename).getroot()
                self.assertEqual(root.attrib, {
                    "width": "34", "height": "34", "viewBox": "0 0 34 34"
                })
                control = filename.split("-")[0].removesuffix(".svg")
                inactive = "inactive" in filename
                hover = "hover" in filename
                elements = list(root)
                self.assertEqual(len(elements), 3 if hover else 2)
                if hover:
                    self.assertEqual(elements.pop(0).attrib, {
                        "x": "2", "y": "2", "width": "29", "height": "29",
                        "rx": "6", "ry": "6", "fill": "#313244",
                    })
                circle, group = elements
                self.assertEqual(circle.tag, namespace + "circle")
                self.assertEqual(circle.attrib, {
                    "cx": "16.5", "cy": "16.5", "r": "7",
                    "fill": "#45475a" if inactive else active_circle[control],
                })
                self.assertEqual(group.tag, namespace + "g")
                self.assertEqual(group.attrib, {
                    "transform": "translate(9 9)",
                    "fill": "#878892" if inactive else "#cdd6f4",
                })
                geometry = glyphs[control]
                self.assertEqual(len(group), len(geometry))
                for element, (tag, attributes) in zip(group, geometry):
                    self.assertEqual(element.tag, namespace + tag)
                    self.assertEqual(element.attrib, attributes)


if __name__ == "__main__":
    unittest.main()
