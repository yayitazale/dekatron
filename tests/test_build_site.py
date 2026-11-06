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


if __name__ == "__main__":
    unittest.main()
