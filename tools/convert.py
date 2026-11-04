"""Build a clock face pack for the data partition of the clock.

Each face is a folder with one image per digit, named 0 to 9 with any image extension.
The images are fitted to the 135x240 panel on a black background.

Usage: python tools/convert.py faces.bin --set Marble=faces/marble --set Neon=faces/neon
"""

import argparse
import io
import struct
import sys
from pathlib import Path

from PIL import Image

WIDTH, HEIGHT = 135, 240
PARTITION_OFFSET = 0x2E0000
PARTITION_BYTES = 0x40000  # the rest of the partition holds the clip library
MAGIC = b"EKFP"
MAX_SETS = 8
DIGITS = 10
NAME_BYTES = 16
SET_HEADER = struct.Struct(f"<{NAME_BYTES}s{DIGITS}I{DIGITS}I")
PACK_HEADER = struct.Struct("<4sI")


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


def build_pack(sets):
    """Pack (name, [10 JPEG byte strings]) sets into the format read by the firmware."""
    if not 1 <= len(sets) <= MAX_SETS:
        raise ValueError(f"Expected 1 to {MAX_SETS} faces, got {len(sets)}")
    position = PACK_HEADER.size + SET_HEADER.size * len(sets)
    headers = []
    payload = bytearray()
    for name, images in sets:
        encoded_name = name.encode("utf-8")
        if not 1 <= len(encoded_name) < NAME_BYTES:
            raise ValueError(f"Invalid face name: {name!r}")
        if len(images) != DIGITS:
            raise ValueError(f"{name}: expected {DIGITS} images, got {len(images)}")
        offsets = []
        for image in images:
            offsets.append(position + len(payload))
            payload += image
        headers.append(SET_HEADER.pack(encoded_name, *offsets, *[len(image) for image in images]))
    pack = PACK_HEADER.pack(MAGIC, len(sets)) + b"".join(headers) + bytes(payload)
    if len(pack) > PARTITION_BYTES:
        raise ValueError(f"The pack takes {len(pack)} bytes but the partition holds {PARTITION_BYTES}")
    return pack


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--set", action="append", required=True, metavar="NAME=FOLDER", dest="sets")
    args = parser.parse_args()

    sets = []
    for entry in args.sets:
        name, separator, folder = entry.partition("=")
        if not separator:
            parser.error(f"Expected NAME=FOLDER: {entry}")
        sets.append((name, load_face(folder)))

    try:
        pack = build_pack(sets)
    except ValueError as error:
        sys.exit(str(error))
    args.output.write_bytes(pack)
    print(f"{args.output}: {len(pack)} of {PARTITION_BYTES} bytes")
    print(f"To flash it: python -m esptool --port COMx write-flash 0x{PARTITION_OFFSET:X} {args.output}")


if __name__ == "__main__":
    main()
