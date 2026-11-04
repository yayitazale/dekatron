import io
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import convert  # noqa: E402


def pack_sets(pack):
    magic, count = convert.PACK_HEADER.unpack_from(pack)
    sets = []
    for index in range(count):
        position = convert.PACK_HEADER.size + index * convert.SET_HEADER.size
        fields = convert.SET_HEADER.unpack_from(pack, position)
        offsets = fields[1:11]
        sizes = fields[11:21]
        name = fields[0].rstrip(b"\0").decode()
        sets.append((name, [pack[o : o + s] for o, s in zip(offsets, sizes)]))
    return magic, sets


class BuildPackTest(unittest.TestCase):
    def test_round_trip_keeps_every_image(self):
        first = [bytes([digit]) * (digit + 1) for digit in range(10)]
        second = [bytes([200 + digit]) * 3 for digit in range(10)]
        magic, sets = pack_sets(convert.build_pack([("Uno", first), ("Dos", second)]))
        self.assertEqual(magic, convert.MAGIC)
        self.assertEqual(sets, [("Uno", first), ("Dos", second)])

    def test_rejects_invalid_packs(self):
        images = [b"x"] * 10
        invalid = [
            [],
            [("Uno", images)] * (convert.MAX_SETS + 1),
            [("", images)],
            [("x" * convert.NAME_BYTES, images)],
            [("Uno", images[:9])],
            [("Uno", [b"x" * convert.PARTITION_BYTES] * 10)],
        ]
        for sets in invalid:
            with self.assertRaises(ValueError):
                convert.build_pack(sets)


class ImageTest(unittest.TestCase):
    def test_fit_image_centres_on_panel_size(self):
        fitted = convert.fit_image(Image.new("RGB", (100, 100), (255, 255, 255)))
        self.assertEqual(fitted.size, (convert.WIDTH, convert.HEIGHT))
        self.assertEqual(fitted.getpixel((0, 0)), (0, 0, 0))
        self.assertEqual(fitted.getpixel((convert.WIDTH // 2, convert.HEIGHT // 2)), (255, 255, 255))

    def test_encoded_jpeg_is_baseline(self):
        data = convert.encode_jpeg(Image.new("RGB", (convert.WIDTH, convert.HEIGHT)))
        with Image.open(io.BytesIO(data)) as image:
            self.assertEqual(image.format, "JPEG")
            self.assertFalse(image.info.get("progressive", False))

    def test_load_face_requires_every_digit(self):
        with tempfile.TemporaryDirectory() as folder:
            for digit in range(9):
                Image.new("RGB", (10, 10)).save(Path(folder) / f"{digit}.png")
            with self.assertRaises(ValueError):
                convert.load_face(folder)
            Image.new("RGB", (10, 10)).save(Path(folder) / "9.png")
            self.assertEqual(len(convert.load_face(folder)), 10)


if __name__ == "__main__":
    unittest.main()
