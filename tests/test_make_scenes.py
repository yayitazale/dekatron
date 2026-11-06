# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import struct
import sys
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import make_clips  # noqa: E402
import make_scenes  # noqa: E402


def unrle(data, expected):
    """Mirrors draw_scene_patch_ in elekstube_ips.cpp."""
    output = bytearray()
    position = 0
    while len(output) < expected:
        token = data[position]
        position += 1
        if token < 128:
            output += bytes([data[position]]) * (token + 1)
            position += 1
        else:
            count = token - 127
            output += data[position : position + count]
            position += count
    return bytes(output)


def replay(scene):
    """Applies every patch to black tubes, as the firmware does, and returns what each tube shows."""
    tubes = [bytearray(make_clips.WIDTH * make_clips.HEIGHT) for _ in range(make_clips.TUBE_COUNT)]
    for record in scene.records:
        count = struct.unpack_from("<H", record)[0]
        for index in range(count):
            tube, x, y, width, height, blob = make_scenes.PATCH.unpack_from(record, 2 + index * make_scenes.PATCH.size)
            pixels = unrle(scene.blobs[blob], width * height)
            for row in range(height):
                start = (y + row) * make_clips.WIDTH + x
                tubes[tube][start : start + width] = pixels[row * width : (row + 1) * width]
    return tubes


def canvas_with_box(left):
    canvas = Image.new("RGB", (make_clips.CANVAS_WIDTH, make_clips.HEIGHT))
    ImageDraw.Draw(canvas).rectangle([left, 50, left + 40, 90], fill=(255, 0, 0))
    return canvas


class RleTest(unittest.TestCase):
    def test_round_trip(self):
        for data in (b"", b"a", b"aaaa", bytes(1000), bytes(range(256)) * 3, b"ab" * 200 + b"c" * 300):
            self.assertEqual(unrle(make_scenes.rle(data), len(data)), data)


class EncodeSceneTest(unittest.TestCase):
    def test_replay_ends_on_the_last_frame(self):
        scene = make_scenes.encode_scene([canvas_with_box(100), canvas_with_box(110), canvas_with_box(120)])
        tubes = replay(scene)
        lit = {index for index, tube in enumerate(tubes) if any(tube)}
        # Image 0 is the leftmost, which is tube 5. The box ends in the gap, so no other tube shows it.
        self.assertEqual(lit, {5})
        red_columns = [x for x in range(make_clips.WIDTH) if tubes[5][70 * make_clips.WIDTH + x]]
        self.assertEqual(min(red_columns), 120)

    def test_only_changes_are_stored(self):
        scene = make_scenes.encode_scene([canvas_with_box(20), canvas_with_box(20)])
        self.assertEqual(struct.unpack_from("<H", scene.records[1])[0], 0)

    def test_a_moving_shape_shares_its_pixels(self):
        # Each step clears the previous box, because a smaller one leaves its middle unchanged and unsent.
        scene = make_scenes.encode_scene([canvas_with_box(left) for left in (0, 45, 90)])
        lit_blobs = set()
        for record in scene.records:
            for index in range(struct.unpack_from("<H", record)[0]):
                _, _, _, width, height, blob = make_scenes.PATCH.unpack_from(record, 2 + index * make_scenes.PATCH.size)
                if any(unrle(scene.blobs[blob], width * height)):
                    lit_blobs.add(blob)
        self.assertEqual(len(lit_blobs), 1)
        tubes = replay(scene)
        red_columns = [x for x in range(make_clips.WIDTH) if tubes[5][70 * make_clips.WIDTH + x]]
        self.assertEqual((min(red_columns), max(red_columns)), (90, 130))

    def test_lights_follow_what_moves_over_the_background(self):
        # A box that crosses tube 5 (the leftmost) and then tube 4, over a still floor.
        floor = Image.new("RGB", (make_clips.CANVAS_WIDTH, make_clips.HEIGHT))
        ImageDraw.Draw(floor).rectangle([0, 200, make_clips.CANVAS_WIDTH, 239], fill=(60, 60, 60))
        frames = []
        for left in (10, 70, make_clips.TUBE_PITCH + 10, make_clips.TUBE_PITCH + 70):
            frame = floor.copy()
            ImageDraw.Draw(frame).rectangle([left, 50, left + 40, 130], fill=(255, 0, 0))
            frames.append(frame)
        track = make_scenes.led_track(frames, floor, smoothing=1.0)
        self.assertEqual(len(track), len(frames) * make_clips.LED_BYTES)
        led = lambda frame, tube: track[frame * make_clips.LED_BYTES + tube * 3 : frame * make_clips.LED_BYTES + tube * 3 + 3]
        # The LED of a tube has its number, and tube 5 is the leftmost one.
        self.assertGreater(led(0, 5)[0], 100)
        self.assertGreater(led(0, 5)[0], 4 * max(led(0, 4)[0], 1))
        self.assertLess(led(0, 5)[1], 30)  # red, with only the little that the floor adds
        self.assertGreater(led(3, 4)[0], 100)
        self.assertGreater(led(3, 4)[0], 4 * max(led(3, 5)[0], 1))

    def test_a_scene_keeps_its_lights_in_a_library(self):
        scene = make_scenes.encode_scene([canvas_with_box(20), canvas_with_box(50)])
        self.assertEqual(len(scene.leds), 2 * make_clips.LED_BYTES)
        library = make_clips.build_library([("scene", 15, scene)])
        _, _, frames, _, per_frame, _, blobs, flags = make_clips.ENTRY.unpack_from(library, make_clips.HEADER.size)
        self.assertEqual((frames, per_frame, flags & 1), (2, 0, 1))
        table = make_clips.ENTRY.unpack_from(library, make_clips.HEADER.size)[1]
        last = make_clips.IMAGE_ENTRY.unpack_from(library, table + (1 + frames + blobs) * make_clips.IMAGE_ENTRY.size)
        self.assertEqual(library[last[0] : last[0] + last[1]], scene.leds)
        with self.assertRaises(ValueError):
            make_clips.build_library([("scene", 15, make_clips.Scene(scene.palette, scene.records, scene.blobs, b"short"))])

    def test_scene_fits_in_a_library(self):
        scene = make_scenes.encode_scene([canvas_with_box(20)])
        library = make_clips.build_library([("scene", 15, scene), ("plain", 15, [[b"a"]])])
        position = make_clips.HEADER.size
        _, _, frames, _, per_frame, _, blobs, _ = make_clips.ENTRY.unpack_from(library, position)
        self.assertEqual((frames, per_frame, blobs), (1, 0, len(scene.blobs)))


if __name__ == "__main__":
    unittest.main()
