import gzip
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import build_web  # noqa: E402


class BuildWebTest(unittest.TestCase):
    def test_page_has_its_style_and_scripts_and_stays_small(self):
        page = build_web.build_page().decode("utf-8")
        self.assertNotIn("/*STYLE*/", page)
        self.assertNotIn("/*SCRIPT*/", page)
        self.assertIn("DekatronStore", page)
        self.assertLess(len(gzip.compress(page.encode("utf-8"), 9)), build_web.LIMIT_BYTES)

    def test_scripts_cannot_end_their_tag_early(self):
        for name in build_web.SCRIPTS:
            self.assertNotIn("</script", (build_web.WEB / name).read_text(encoding="utf-8"))

    def test_the_header_in_the_firmware_is_the_current_page(self):
        data = gzip.compress(build_web.build_page(), compresslevel=9, mtime=0)
        self.assertEqual(build_web.to_header(data), build_web.OUTPUT.read_text(encoding="utf-8"),
                         "web/ changed: run python tools/build_web.py")


if __name__ == "__main__":
    unittest.main()
