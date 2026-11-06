# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a theme file from your own animations.

Usage:
  python tools/make_theme.py surf.theme.bin --name Surf --author "Your name" --license CC-BY-SA-4.0 \\
      --boot boot.gif --connecting waiting.gif --online connected.gif --countdown_end finale.gif

A theme is four animations that the clock always needs: boot (when it starts), connecting (while it looks
for the Wi-Fi), online (when it connects) and countdown_end (when a countdown ends). Each one can be an
animated GIF, WebP or PNG, a video (needs ffmpeg on the PATH) or a folder of numbered pictures. The picture
is one strip that crosses the six tubes: 1370 x 240 pixels, tube 5 on the left, with 112 pixels between
tubes that are never shown. Other sizes are scaled to fill the strip and cropped.

The tool stores only what changes between frames, with one palette of 255 colors for the four animations,
and can give each tube's light the color of its picture. It checks the result with tools/check_bin.py and
prints the problems, if any. In the page of the clock, Change theme takes the file in place of the theme of
the clock and keeps the faces. See docs/making-bins.md.
"""

import argparse
import sys
from pathlib import Path

import check_bin
from make_clips import CANVAS_WIDTH, HEIGHT, THEME_ROLES, REGION_BYTES, build_library, cover, read_frames
from make_scenes import build_palette, encode_scene

MAX_FRAMES = 450  # 30 seconds at 15 fps
MAX_FPS = 20


def load_canvases(path, fps):
    frames = read_frames(path, fps)
    if len(frames) > MAX_FRAMES:
        raise ValueError(f"{path}: {len(frames)} frames; the limit is {MAX_FRAMES}. Cut it or lower --fps.")
    return [cover(frame, CANVAS_WIDTH, HEIGHT) for frame in frames]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", required=True, help="name of the theme, up to 15 bytes")
    parser.add_argument("--author", required=True)
    parser.add_argument("--license", required=True, help='for the gallery of the project: "CC-BY-SA-4.0"')
    parser.add_argument("--description", default="")
    for role in THEME_ROLES:
        parser.add_argument(f"--{role}", required=True, type=Path, metavar="FILE", help=f"animation for {role}")
    parser.add_argument("--fps", type=int, default=15, help=f"frames per second, 1 to {MAX_FPS} (default 15)")
    parser.add_argument("--no-lights", action="store_true", help="leave the lights of the tubes off")
    parser.add_argument("--gallery", action="store_true", help="also check what the gallery of the project asks")
    args = parser.parse_args()
    if not 1 <= args.fps <= MAX_FPS:
        parser.error(f"--fps takes 1 to {MAX_FPS}")

    info = {"author": args.author, "license": args.license}
    if args.description:
        info["description"] = args.description
    try:
        made = [(role, load_canvases(getattr(args, role), args.fps)) for role in THEME_ROLES]
        entries = build_palette([canvases for _, canvases in made])
        clips = []
        for role, canvases in made:
            scene = encode_scene(canvases, entries, lights=not args.no_lights)
            size = len(scene.palette) + sum(map(len, scene.records)) + sum(map(len, scene.blobs))
            print(f"{role}: {len(canvases)} frames at {args.fps} fps, {size} bytes")
            clips.append((role, args.fps, scene))
        library = build_library(clips, theme=(args.name, info))
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
