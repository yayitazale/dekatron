# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import build_site  # noqa: E402


class BuildSiteTest(unittest.TestCase):
    def test_the_creator_has_its_style_and_scripts(self):
        page = build_site.build_creator()
        for marker in ("/*STYLE*/", "/*SCRIPT*/"):
            self.assertNotIn(marker, page)
        for name in ("DekatronStore", "DekatronScene", "DekatronCreator"):
            self.assertIn(name, page)

    def test_the_page_in_the_site_is_the_current_one(self):
        self.assertEqual(build_site.build_creator(), build_site.OUTPUT.read_text(encoding="utf-8"),
                         "site/ changed: run python tools/build_site.py")


    def test_the_landing_page_has_its_style_and_script(self):
        page = build_site.build_landing()
        for marker in ("/*STYLE*/", "/*SCRIPT*/"):
            self.assertNotIn(marker, page)
        for text in ("body.site", "id=\"gallery\"", "themes/index.json", "no affiliation with EleksTube"):
            self.assertIn(text, page)

    def test_the_landing_page_in_the_site_is_the_current_one(self):
        self.assertEqual(build_site.build_landing(), build_site.LANDING_OUTPUT.read_text(encoding="utf-8"),
                         "site/ changed: run python tools/build_site.py")

    def test_the_sections_of_the_landing_page_are_reachable(self):
        page = build_site.build_landing()
        for section in ("install", "use", "update", "themes", "create", "ai", "safe"):
            self.assertIn(f'id="{section}"', page)


if __name__ == "__main__":
    unittest.main()
