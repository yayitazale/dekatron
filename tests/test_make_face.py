# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_bin  # noqa: E402


def font_file():
    """Any TrueType font on this machine; the tests are skipped without one."""
    for candidate in ("C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                      "/System/Library/Fonts/Supplemental/Arial.ttf"):
        if Path(candidate).is_file():
            return candidate
    return None


class MakeFaceTest(unittest.TestCase):
    def run_tool(self, output, *extra):
        args = [sys.executable, str(ROOT / "tools" / "make_face.py"), str(output), "--name", "Test", "--author", "Tester",
                "--license", "CC-BY-SA-4.0", "--gallery", *extra]
        return subprocess.run(args, capture_output=True, text=True, cwd=ROOT)

    def test_a_face_from_pictures_passes_the_checker(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name) / "digits"
            folder.mkdir()
            for digit in range(10):
                image = Image.new("RGB", (80, 120))
                ImageDraw.Draw(image).text((20, 40), str(digit), fill=(255, 255, 255), font=ImageFont.load_default(40))
                image.save(folder / f"{digit}.png")
            output = Path(name) / "test.face.bin"
            result = self.run_tool(output, "--images", str(folder))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = check_bin.check(output.read_bytes(), gallery=True)
            self.assertEqual((report.ok, report.type, report.name), (True, "face", "Test"))

    @unittest.skipUnless(font_file(), "no font file found")
    def test_a_face_from_a_font_passes_the_checker(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "test.face.bin"
            result = self.run_tool(output, "--font", font_file(), "--color", "#00ff66")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(check_bin.check(output.read_bytes(), gallery=True).ok)

    def test_a_missing_picture_is_reported(self):
        with tempfile.TemporaryDirectory() as name:
            result = self.run_tool(Path(name) / "x.bin", "--images", name)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("digit 0", result.stderr)


if __name__ == "__main__":
    unittest.main()
