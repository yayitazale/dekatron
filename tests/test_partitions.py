# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from make_clips import FLASH_OFFSET, REGION_BYTES  # noqa: E402

# The table of partitions cannot be changed on a clock over the air: a different one would have to be written
# over USB, and a store or an app in the wrong place breaks the clock. It is frozen for the 0.x releases. If
# you really need to change it, this is the place to say so, and the release notes must say how to update.
FROZEN = [
    ("otadata", "data", "ota", 0x9000, 0x2000),
    ("phy_init", "data", "phy", 0xB000, 0x1000),
    ("nvs", "data", "nvs", 0xC000, 0x14000),
    ("app0", "app", "ota_0", 0x20000, 0x160000),
    ("app1", "app", "ota_1", 0x180000, 0x160000),
    ("clips", "data", "0x40", 0x2E0000, 0x120000),
]


def read_table():
    rows = []
    for line in (ROOT / "firmware" / "partitions.csv").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            name, kind, subtype, offset, size = [part.strip() for part in line.split(",")]
            rows.append((name, kind, subtype, int(offset, 16), int(size, 16)))
    return rows


class PartitionsTest(unittest.TestCase):
    def test_the_table_is_the_frozen_one(self):
        self.assertEqual(read_table(), FROZEN, "firmware/partitions.csv changed: see the comment in this test")

    def test_the_partitions_do_not_overlap_and_fit_in_four_megabytes(self):
        end = 0
        for name, _, _, offset, size in sorted(read_table(), key=lambda row: row[3]):
            self.assertGreaterEqual(offset, end, f"{name} overlaps the one before")
            end = offset + size
        self.assertLessEqual(end, 0x400000)

    def test_the_store_is_where_the_tools_write_it(self):
        clips = next(row for row in read_table() if row[0] == "clips")
        self.assertEqual((clips[3], clips[4]), (FLASH_OFFSET, REGION_BYTES))


if __name__ == "__main__":
    unittest.main()
