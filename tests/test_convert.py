import io
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import convert  # noqa: E402


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
