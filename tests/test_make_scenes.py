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

    def test_scene_fits_in_a_library(self):
        scene = make_scenes.encode_scene([canvas_with_box(20)])
        library = make_clips.build_library([("scene", 15, scene), ("plain", 15, [[b"a"]])])
        position = make_clips.HEADER.size
        _, _, frames, _, per_frame, _, blobs, _ = make_clips.ENTRY.unpack_from(library, position)
        self.assertEqual((frames, per_frame, blobs), (1, 0, len(scene.blobs)))


if __name__ == "__main__":
    unittest.main()
