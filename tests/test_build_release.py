# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "tools"))

import build_release  # noqa: E402
import check_cases  # noqa: E402
from make_clips import FLASH_OFFSET  # noqa: E402

CHANGELOG = """# Changelog

## [Unreleased]

## [0.2.0] - 2026-12-01

### Added

- **web**: Something new (abc12345)

## [0.1.0] - 2026-11-20

First version.
"""


def fake_app(size=0x131d90):
    """Shaped like the image that ESPHome makes: erased flash, bootloader, partitions, and the app at 0x20000."""
    image = bytearray(b"\xff" * size)
    image[0x1000] = 0xE9
    image[0x8000:0x8002] = b"\xaa\x50"
    image[0x20000] = 0xE9
    return bytes(image)


def make_inputs(root, ota=None, theme_info=None):
    firmware, themes = root / "firmware", root / "themes"
    for profile in build_release.PROFILES:
        (firmware / profile).mkdir(parents=True)
        (firmware / profile / "firmware.ota.bin").write_bytes(ota or check_cases.firmware())
        (firmware / profile / "firmware.factory.bin").write_bytes(fake_app())
    themes.mkdir()
    (themes / "space.theme.bin").write_bytes(check_cases.theme(info=theme_info or check_cases.INFO))
    return firmware, themes


class BuildReleaseTest(unittest.TestCase):
    def test_the_notes_of_a_version_are_its_section_of_the_changelog(self):
        self.assertEqual(build_release.changelog_section(CHANGELOG, "0.2.0"), "### Added\n\n- **web**: Something new (abc12345)")
        self.assertEqual(build_release.changelog_section(CHANGELOG, "0.1.0"), "First version.")
        self.assertEqual(build_release.changelog_section(CHANGELOG, "9.9.9"), "")

    def test_the_factory_image_has_the_app_then_the_store_where_the_clock_reads_it(self):
        image = build_release.factory_image(fake_app(), b"STORE")
        self.assertEqual(len(image), FLASH_OFFSET + 5)
        self.assertEqual(image[FLASH_OFFSET:], b"STORE")
        self.assertEqual(image[0x131d90:FLASH_OFFSET], b"\xff" * (FLASH_OFFSET - 0x131d90))
        self.assertEqual(image[0x20000], 0xE9)
        with self.assertRaises(ValueError):
            build_release.factory_image(b"x" * (FLASH_OFFSET + 1), b"")

    def test_a_release_has_its_files_its_checksums_and_the_versions_file_of_the_site(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            firmware, themes = make_inputs(root)
            out, site = root / "out", root / "site"
            site.mkdir()
            versions = build_release.build("0.2.0", firmware, themes, out, site, "2026-12-01", CHANGELOG)
            names = sorted(path.name for path in out.iterdir())
            self.assertEqual(names, ["SHA256SUMS", "dekatron-0.2.0-clock.factory.bin", "dekatron-0.2.0-clock.ota.bin",
                                     "dekatron-0.2.0-full.factory.bin", "dekatron-0.2.0-full.ota.bin", "space.theme.bin"])
            factory = (out / "dekatron-0.2.0-full.factory.bin").read_bytes()
            self.assertEqual(factory[FLASH_OFFSET:], (themes / "space.theme.bin").read_bytes())
            for line in (out / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
                digest, file = line.split("  ")
                self.assertEqual(hashlib.sha256((out / file).read_bytes()).hexdigest(), digest)
            on_disk = json.loads((site / "versions.json").read_text(encoding="utf-8"))
            self.assertEqual(on_disk, versions)
            self.assertEqual((on_disk["latest"], on_disk["date"]), ("0.2.0", "2026-12-01"))
            self.assertIn("Something new", on_disk["notes"])
            full = on_disk["firmware"]["full"]
            self.assertEqual(full["url"], "https://yayitazale.github.io/dekatron/firmware/v0.2.0/dekatron-0.2.0-full.ota.bin")
            copy = site / "firmware" / "v0.2.0" / "dekatron-0.2.0-full.ota.bin"
            self.assertEqual(hashlib.sha256(copy.read_bytes()).hexdigest(), full["sha256"])

    def test_what_the_page_of_the_clock_reads_is_what_it_accepts(self):
        # The same rules as web/update.js: a newer version, an https address and a 64-digit checksum.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            firmware, themes = make_inputs(root)
            site = root / "site"
            site.mkdir()
            versions = build_release.build("0.2.0", firmware, themes, root / "out", site, "2026-12-01", CHANGELOG)
            for entry in versions["firmware"].values():
                self.assertTrue(entry["url"].startswith("https://"))
                self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")

    def test_nothing_is_written_when_a_file_is_wrong(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            firmware, themes = make_inputs(root, ota=check_cases.firmware(chip=5))
            with self.assertRaises(ValueError):
                build_release.build("0.2.0", firmware, themes, root / "out", root / "site", "2026-12-01")
            self.assertFalse((root / "out").exists())
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            firmware, themes = make_inputs(root, theme_info={"author": "A", "license": "All rights reserved"})
            with self.assertRaises(ValueError):
                build_release.build("0.2.0", firmware, themes, root / "out", root / "site", "2026-12-01")
            self.assertFalse((root / "out").exists())


if __name__ == "__main__":
    unittest.main()
