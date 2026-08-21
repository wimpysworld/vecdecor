#!/usr/bin/env python3

import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


METADATA_PATH = Path(sys.argv.pop())


class SizeMetadataTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
