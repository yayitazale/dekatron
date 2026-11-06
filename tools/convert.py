# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Image helpers for the tools that build the store of the clock.

A face is a folder with one image per digit, named 0 to 9 with any image extension. The images are
fitted to the 135x240 panel on a black background. tools/make_clips.py packs them.
"""

import io
from pathlib import Path

from PIL import Image

WIDTH, HEIGHT = 135, 240
DIGITS = 10


def fit_image(image):
    image = image.convert("RGB")
    scale = min(WIDTH / image.width, HEIGHT / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    canvas = Image.new("RGB", (WIDTH, HEIGHT))
    canvas.paste(image.resize(size, Image.LANCZOS), ((WIDTH - size[0]) // 2, (HEIGHT - size[1]) // 2))
    return canvas


def encode_jpeg(image, quality=85):
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=quality, subsampling="4:2:0", progressive=False, optimize=False)
    return buffer.getvalue()


def load_face(folder):
    images = []
    for digit in range(DIGITS):
        matches = [path for path in Path(folder).glob(f"{digit}.*") if path.is_file()]
        if len(matches) != 1:
            raise ValueError(f"{folder}: expected one image for digit {digit}, found {len(matches)}")
        with Image.open(matches[0]) as image:
            images.append(encode_jpeg(fit_image(image)))
    return images
