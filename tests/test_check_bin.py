# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import check_cases  # noqa: E402


class CheckBinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.answers = check_cases.answers()

    def expect(self, name, kind, problems=(), warnings=()):
        answer = self.answers[name]
        self.assertEqual((answer["type"], answer["problems"], answer["warnings"]), (kind, list(problems), list(warnings)), name)

    def test_good_files_pass_and_say_what_they_are(self):
        self.expect("good-face", "face")
        self.expect("good-theme", "theme")
        self.expect("good-theme-gallery", "theme")
        self.expect("backup", "backup")
        self.expect("firmware", "firmware")

    def test_the_type_comes_from_the_content(self):
        self.expect("backup-two-faces", "backup")
        self.expect("face-without-info", "face", warnings=["info-author", "info-license"])

    def test_pictures_the_clock_cannot_read_are_refused(self):
        self.expect("face-progressive", "face", ["jpeg-not-baseline"])
        self.expect("face-wrong-size", "face", ["jpeg-size"])
        self.expect("face-not-jpeg", "face", ["jpeg-invalid"])
        self.expect("face-444", "face", warnings=["jpeg-subsampling"])

    def test_a_theme_has_its_four_animations_and_valid_scenes(self):
        self.expect("theme-missing-clip", "theme", ["theme-missing-clip"])
        self.expect("theme-extra-clip", "theme", ["theme-extra-clip"])
        self.expect("theme-bad-patch", "theme", ["scene-invalid"] * 4)
        self.expect("theme-good-patch", "theme")

    def test_the_gallery_asks_for_author_and_license(self):
        self.expect("face-another-license", "face", ["gallery-license"])
        self.expect("theme-no-info", "theme", ["gallery-author", "gallery-license"], ["info-author", "info-license"])

    def test_damaged_files_say_why(self):
        self.expect("not-a-store", "unknown", ["not-a-store"])
        self.expect("too-short", "unknown", ["not-a-store"])
        self.expect("bad-crc", "unknown", ["bad-crc"])
        self.expect("bad-version", "unknown", ["version"])
        self.expect("cut-short", "unknown", ["size-mismatch"])

    def test_firmware_for_another_chip_or_too_big_is_refused(self):
        self.expect("firmware-other-chip", "firmware", ["firmware-chip"])
        self.expect("firmware-too-small", "firmware", ["firmware-size"])
        self.expect("firmware-too-big", "firmware", ["firmware-size"])

    def test_the_space_theme_of_the_project_is_fit_for_the_gallery(self):
        import check_bin
        import make_scenes
        self.assertEqual(make_scenes.THEME[1]["license"], check_bin.GALLERY_LICENSE)


if __name__ == "__main__":
    unittest.main()
