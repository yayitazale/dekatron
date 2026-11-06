# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import binascii
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import make_clips  # noqa: E402


def parse_library(library):
    magic, version, total, crc, count = make_clips.HEADER.unpack_from(library)
    clips = {}
    for index in range(count):
        position = make_clips.HEADER.size + index * make_clips.ENTRY.size
        name, table, frames, fps, per_frame, _, _, _ = make_clips.ENTRY.unpack_from(library, position)
        images = []
        for image_index in range(frames * per_frame):
            offset, size = make_clips.IMAGE_ENTRY.unpack_from(library, table + image_index * make_clips.IMAGE_ENTRY.size)
            images.append(library[offset : offset + size])
        clips[name.rstrip(b"\0").decode()] = (fps, per_frame, images)
    return magic, version, total, crc, clips


class BuildLibraryTest(unittest.TestCase):
    def test_round_trip_and_crc(self):
        library = make_clips.build_library([("one", 15, [[b"a"], [b"bb"]]), ("two", 4, [[b"c"] * 6])])
        magic, version, total, crc, clips = parse_library(library)
        self.assertEqual((magic, version, total), (make_clips.MAGIC, make_clips.VERSION, len(library)))
        self.assertEqual(crc, binascii.crc32(library[make_clips.HEADER.size :]))
        self.assertEqual(clips["one"], (15, 1, [b"a", b"bb"]))
        self.assertEqual(clips["two"], (4, 6, [b"c"] * 6))

    def test_faces_and_clips_share_one_store(self):
        digits = [bytes([digit]) * 3 for digit in range(10)]
        library = make_clips.build_library([("one", 15, [[b"a"]])], [("Neon", digits), ("Ice", digits)])
        _, _, total, _, clips = parse_library(library)
        self.assertEqual(total, len(library))
        self.assertEqual(sorted(clips), ["Ice", "Neon", "one"])
        self.assertEqual(clips["Neon"][1:], (10, digits))
        kinds = [make_clips.ENTRY.unpack_from(library, make_clips.HEADER.size + i * make_clips.ENTRY.size)[5] for i in range(3)]
        self.assertEqual(kinds, [make_clips.KIND_FACE, make_clips.KIND_FACE, make_clips.KIND_CLIP])

    def test_a_theme_is_stored_with_its_data(self):
        info = {"author": "Someone", "license": "MIT"}
        library = make_clips.build_library([("boot", 15, [[b"a"]])], theme=("Space", info))
        _, _, total, _, entries = parse_library(library)
        self.assertEqual(sorted(entries), ["Space", "boot"])
        fps, per_frame, images = entries["Space"]
        self.assertEqual(per_frame, 1)
        self.assertEqual(json.loads(images[0]), info)
        kinds = [make_clips.ENTRY.unpack_from(library, make_clips.HEADER.size + i * make_clips.ENTRY.size)[5] for i in range(2)]
        self.assertEqual(kinds, [make_clips.KIND_THEME, make_clips.KIND_CLIP])

    def test_a_store_holds_a_few_faces_and_one_theme(self):
        digits = [b"x"] * 10
        make_clips.build_library([], [(f"Face{i}", digits) for i in range(make_clips.MAX_FACES)])
        with self.assertRaises(ValueError):
            make_clips.build_library([], [(f"Face{i}", digits) for i in range(make_clips.MAX_FACES + 1)])
        with self.assertRaises(ValueError):
            make_clips.build_library([], theme=("Big", {"description": "x" * make_clips.MAX_THEME_INFO}))

    def test_identical_images_are_stored_once(self):
        image = b"x" * 1000
        library = make_clips.build_library([("loop", 15, [[image]] * 10)])
        self.assertLess(len(library), 2000)

    def test_rejects_invalid_libraries(self):
        invalid = [
            [],
            [("", 15, [[b"a"]])],
            [("x" * make_clips.NAME_BYTES, 15, [[b"a"]])],
            [("a", 15, [[b"a"]]), ("a", 15, [[b"a"]])],
            [("a", 15, [])],
            [("a", 0, [[b"a"]])],
            [("a", 15, [[b"a", b"b"]])],
            [("a", 15, [[b"a"], [b"b"] * 6])],
            [("a", 15, [[bytes(make_clips.REGION_BYTES)]])],
        ]
        for faces in ([("Neon", [b"x"] * 9)], [("", [b"x"] * 10)], [("Neon", [b"x"] * 10)] * 2):
            with self.assertRaises(ValueError):
                make_clips.build_library([], faces)
        for clips in invalid:
            with self.assertRaises(ValueError):
                make_clips.build_library(clips)


class FrameTest(unittest.TestCase):
    def test_gif_is_resampled_to_the_requested_fps(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "clip.gif"
            first = Image.new("RGB", (20, 20), (255, 0, 0))
            second = Image.new("RGB", (20, 20), (0, 0, 255))
            first.save(path, save_all=True, append_images=[second], duration=200, loop=0)
            frames = make_clips.read_frames(path, 10)
        self.assertEqual(len(frames), 4)
        self.assertGreater(frames[0].getpixel((0, 0))[0], 200)
        self.assertGreater(frames[3].getpixel((0, 0))[2], 200)

    def test_panorama_leaves_out_the_gaps(self):
        canvas = Image.new("RGB", (make_clips.CANVAS_WIDTH, make_clips.HEIGHT))
        encoded = make_clips.encode_panorama([canvas], 70)
        self.assertEqual(len(encoded[0]), make_clips.TUBE_COUNT)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tube.jpg"
            path.write_bytes(encoded[0][5])
            with Image.open(path) as tube:
                self.assertEqual(tube.size, (make_clips.WIDTH, make_clips.HEIGHT))


if __name__ == "__main__":
    unittest.main()
