# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a clock face file from a font or from your own pictures.

Usage:
  python tools/make_face.py neon.face.bin --name Neon --author "Your name" --license CC-BY-SA-4.0 \\
      --font MyFont.ttf --color "#00ff66"
  python tools/make_face.py marble.face.bin --name Marble --author "Your name" --license CC-BY-SA-4.0 \\
      --images folder/

A face is ten pictures of 135 x 240 pixels, one per digit. With --font they are drawn from a TrueType or
OpenType font file, in a color with a glow, on a background. With --images the folder has one picture for each
digit named 0 to 9 with any image extension (0.png ... 9.png), and each is fitted to the tube on black.
The font must allow what you do with the face: the file carries its license in --license, and the gallery
of the project takes CC-BY-SA-4.0. The result is checked with tools/check_bin.py. See docs/making-bins.md.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageColor, ImageDraw, ImageFilter, ImageFont

import check_bin
from convert import DIGITS, HEIGHT, WIDTH, encode_jpeg, load_face
from make_clips import REGION_BYTES, build_library


def render_digit(digit, font, glow, background, glow_amount):
    core = tuple(round(channel + (255 - channel) * 0.6) for channel in glow)
    mask = Image.new("L", (WIDTH, HEIGHT))
    ImageDraw.Draw(mask).text((WIDTH / 2, HEIGHT / 2), str(digit), fill=255, font=font, anchor="mm")
    image = Image.new("RGB", (WIDTH, HEIGHT), background)
    if glow_amount:
        image.paste(Image.new("RGB", image.size, glow), mask=mask.filter(ImageFilter.GaussianBlur(9)).point(lambda v: v * glow_amount))
        image.paste(Image.new("RGB", image.size, glow), mask=mask.filter(ImageFilter.GaussianBlur(3)).point(lambda v: v * glow_amount))
    image.paste(Image.new("RGB", image.size, core), mask=mask)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", required=True, help="name of the face, up to 15 bytes")
    parser.add_argument("--author", required=True)
    parser.add_argument("--license", required=True, help='for the gallery of the project: "CC-BY-SA-4.0"')
    parser.add_argument("--description", default="")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--font", type=Path, help="a .ttf or .otf file")
    source.add_argument("--images", type=Path, help="a folder with 0.png to 9.png")
    parser.add_argument("--color", default="#ff6e14", help="color of the digits with --font (default #ff6e14)")
    parser.add_argument("--background", default="#000000", help="background with --font (default black)")
    parser.add_argument("--size", type=int, default=200, help="size of the digits in pixels with --font (default 200)")
    parser.add_argument("--glow", type=float, default=1.0, help="strength of the glow with --font, 0 for none (default 1)")
    parser.add_argument("--gallery", action="store_true", help="also check what the gallery of the project asks")
    args = parser.parse_args()

    info = {"author": args.author, "license": args.license}
    if args.description:
        info["description"] = args.description
    try:
        if args.font:
            font = ImageFont.truetype(str(args.font), args.size)
            glow = ImageColor.getrgb(args.color)
            background = ImageColor.getrgb(args.background)
            images = [encode_jpeg(render_digit(digit, font, glow, background, args.glow)) for digit in range(DIGITS)]
        else:
            images = load_face(args.images)
        library = build_library([], [(args.name, images, info)])
    except (ValueError, OSError) as error:
        sys.exit(str(error))

    args.output.write_bytes(library)
    print(f"{args.output}: {len(library)} of {REGION_BYTES} bytes")
    report = check_bin.check(library, args.gallery)
    for rule, message in report.problems:
        print(f"problem  {rule}: {message}")
    for rule, message in report.warnings:
        print(f"warning  {rule}: {message}")
    sys.exit(0 if report.ok else 1)


if __name__ == "__main__":
    main()
