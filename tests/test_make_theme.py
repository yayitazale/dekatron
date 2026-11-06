# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_bin  # noqa: E402
import make_clips  # noqa: E402


def moving_square(count, size=(274, 48), color=(255, 140, 0)):
    frames = []
    for index in range(count):
        frame = Image.new("RGB", size)
        x = index * (size[0] - 20) // count
        ImageDraw.Draw(frame).rectangle((x, 10, x + 20, 30), fill=color)
        frames.append(frame)
    return frames


def save_gif(path, frames):
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=67, loop=0)


class MakeThemeTest(unittest.TestCase):
    def run_tool(self, folder, *extra):
        output = folder / "test.theme.bin"
        args = [sys.executable, str(ROOT / "tools" / "make_theme.py"), str(output), "--name", "Squares", "--author", "Tester",
                "--license", "CC-BY-SA-4.0", *extra]
        result = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
        return result, output

    def test_a_theme_from_gifs_and_a_folder_passes_the_checker(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            roles = {}
            for number, role in enumerate(make_clips.THEME_ROLES[:3]):
                save_gif(folder / f"{role}.gif", moving_square(6, color=(255, 40 * number, 0)))
                roles[role] = folder / f"{role}.gif"
            numbered = folder / "end"
            numbered.mkdir()
            for index, frame in enumerate(moving_square(5, color=(0, 200, 255)), 1):
                frame.save(numbered / f"{index:04d}.png")
            result, output = self.run_tool(folder, "--boot", str(roles["boot"]), "--connecting", str(roles["connecting"]),
                                           "--online", str(roles["online"]), "--countdown_end", str(numbered), "--gallery")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = check_bin.check(output.read_bytes(), gallery=True)
            self.assertTrue(report.ok, report.problems)
            self.assertEqual((report.type, report.name), ("theme", "Squares"))
            self.assertEqual(report.info["author"], "Tester")

    def test_a_missing_animation_is_reported(self):
        with tempfile.TemporaryDirectory() as name:
            result, _ = self.run_tool(Path(name), "--boot", "x.gif")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--connecting", result.stderr)


if __name__ == "__main__":
    unittest.main()
