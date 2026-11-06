# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "tools"))

import check_bin  # noqa: E402
from make_clips import THEME_ROLES  # noqa: E402
from test_make_theme import moving_square, save_gif  # noqa: E402


def make_theme_folder(themes, name, license="CC-BY-SA-4.0", roles=THEME_ROLES):
    folder = themes / name
    folder.mkdir(parents=True)
    sources = {}
    for number, role in enumerate(roles):
        save_gif(folder / f"{role}.gif", moving_square(6, color=(255, 30 * number, 40)))
        sources[role] = f"{role}.gif"
    (folder / "theme.json").write_text(json.dumps({"name": name.title(), "author": "Tester", "license": license,
                                                   "description": "Squares", "sources": sources}), encoding="utf-8")


def run(themes, out, *ids):
    args = [sys.executable, str(ROOT / "tools" / "build_themes.py"), "--themes", str(themes), "--out", str(out), *ids]
    return subprocess.run(args, capture_output=True, text=True, cwd=ROOT)


class BuildThemesTest(unittest.TestCase):
    def test_a_theme_from_pictures_gets_its_file_preview_and_index_entry(self):
        with tempfile.TemporaryDirectory() as name:
            themes, out = Path(name) / "themes", Path(name) / "out"
            make_theme_folder(themes, "surf")
            result = run(themes, out)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            entry = json.loads((out / "index.json").read_text(encoding="utf-8"))["themes"][0]
            self.assertEqual((entry["id"], entry["name"], entry["author"], entry["license"]), ("surf", "Surf", "Tester", "CC-BY-SA-4.0"))
            data = (out / entry["file"]).read_bytes()
            self.assertEqual(entry["bytes"], len(data))
            self.assertTrue(check_bin.check(data, gallery=True).ok)
            self.assertTrue((out / entry["preview"]).read_bytes().startswith(b"GIF8"))

    def test_a_theme_that_the_gallery_would_not_take_stops_the_build(self):
        with tempfile.TemporaryDirectory() as name:
            themes = Path(name) / "themes"
            make_theme_folder(themes, "closed", license="All rights reserved")
            result = run(themes, Path(name) / "out")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("gallery-license", result.stderr)

    def test_a_theme_without_an_animation_stops_the_build(self):
        with tempfile.TemporaryDirectory() as name:
            themes = Path(name) / "themes"
            make_theme_folder(themes, "short", roles=THEME_ROLES[:3])
            result = run(themes, Path(name) / "out")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("countdown_end", result.stderr)

    def test_the_space_theme_is_listed(self):
        config = json.loads((ROOT / "themes" / "space" / "theme.json").read_text(encoding="utf-8"))
        self.assertEqual(config["builder"], "make_scenes")


if __name__ == "__main__":
    unittest.main()
