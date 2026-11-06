# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import gzip
import re
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
        # What is compared is the page inside the header, not its compressed bytes, which depend on the zlib
        # that Python was built with.
        header = build_web.OUTPUT.read_text(encoding="utf-8")
        packed = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", header.split("= {", 1)[1]))
        self.assertEqual(gzip.decompress(packed), build_web.build_page(), "web/ changed: run python tools/build_web.py")


if __name__ == "__main__":
    unittest.main()
