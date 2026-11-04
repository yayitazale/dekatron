"""Build the clip library for the clock from GIF, animated WebP or video files.

Usage: python tools/make_clips.py clips.bin --clip alarm=alarm.gif --clip wave=wave.mp4:panorama --fps 15

A clip shows the same picture on every tube, or with ":panorama" one picture across all six,
cropped to leave out the gap between screens. Panoramas play at only 3 to 4 fps. Videos need
ffmpeg on the PATH.

Serve the file over HTTP and publish {"url": "http://host/clips.bin"} to
elekstube/elekstube/clips/download, or flash it over USB at 0x320000.
"""

import argparse
import binascii
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageSequence

from convert import HEIGHT, WIDTH, encode_jpeg, fit_image

REGION_BYTES = 0xE0000
FLASH_OFFSET = 0x320000
MAGIC = b"EKCL"
VERSION = 1
NAME_BYTES = 16
HEADER = struct.Struct("<4sIIII")  # magic, version, total size, CRC32 of the rest, clip count
CLIP_ENTRY = struct.Struct(f"<{NAME_BYTES}sIIHB3x")  # name, table offset, frames, fps, images per frame
IMAGE_ENTRY = struct.Struct("<II")  # offset, size

# Must match TUBE_GAP_PIXELS in animation.h.
TUBE_PITCH = WIDTH + 112
TUBE_COUNT = 6
CANVAS_WIDTH = (TUBE_COUNT - 1) * TUBE_PITCH + WIDTH


def read_frames(path, fps):
    """Returns (PIL images, seconds per image) resampled to `fps`."""
    path = Path(path)
    if path.suffix.lower() in (".gif", ".webp", ".png", ".apng"):
        return sample_animation(path, fps)
    return read_video(path, fps)


def sample_animation(path, fps):
    with Image.open(path) as image:
        sources = []
        for frame in ImageSequence.Iterator(image):
            sources.append((frame.convert("RGB"), frame.info.get("duration", 100) or 100))
    total_ms = sum(duration for _, duration in sources)
    count = max(1, round(total_ms * fps / 1000))
    frames = []
    index = 0
    end_ms = sources[0][1]
    for output in range(count):
        time_ms = output * 1000 / fps
        while time_ms >= end_ms and index < len(sources) - 1:
            index += 1
            end_ms += sources[index][1]
        frames.append(sources[index][0])
    return frames


def read_video(path, fps):
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise ValueError(f"{path}: reading video needs ffmpeg on the PATH, or convert it to GIF")
    with tempfile.TemporaryDirectory() as folder:
        subprocess.run([ffmpeg, "-loglevel", "error", "-i", str(path), "-vf", f"fps={fps}", f"{folder}/%05d.png"],
                       check=True)
        frames = []
        for frame_path in sorted(Path(folder).glob("*.png")):
            with Image.open(frame_path) as frame:
                frames.append(frame.convert("RGB"))
    if not frames:
        raise ValueError(f"{path}: no frames found")
    return frames


def cover(image, width, height):
    scale = max(width / image.width, height / image.height)
    size = (max(width, round(image.width * scale)), max(height, round(image.height * scale)))
    resized = image.resize(size, Image.LANCZOS)
    left = (size[0] - width) // 2
    top = (size[1] - height) // 2
    return resized.crop((left, top, left + width, top + height))


def encode_common(frames, quality):
    return [[encode_jpeg(fit_image(frame), quality)] for frame in frames]


def encode_panorama(frames, quality):
    encoded = []
    for frame in frames:
        canvas = cover(frame, CANVAS_WIDTH, HEIGHT)
        # Images are stored left to right: tube 5 first.
        encoded.append([encode_jpeg(canvas.crop((index * TUBE_PITCH, 0, index * TUBE_PITCH + WIDTH, HEIGHT)), quality)
                        for index in range(TUBE_COUNT)])
    return encoded


def build_library(clips):
    """Packs (name, fps, [frames of JPEG byte strings]) clips, sharing identical images."""
    if not clips:
        raise ValueError("Expected at least one clip")
    names = [name for name, _, _ in clips]
    if len(set(names)) != len(names):
        raise ValueError("Clip names must be unique")

    position = HEADER.size + CLIP_ENTRY.size * len(clips)
    tables = []
    for name, fps, frames in clips:
        encoded_name = name.encode("utf-8")
        if not 1 <= len(encoded_name) < NAME_BYTES:
            raise ValueError(f"Invalid clip name: {name!r}")
        if not frames or not 1 <= fps <= 60:
            raise ValueError(f"{name}: invalid frames or fps")
        images_per_frame = len(frames[0])
        if images_per_frame not in (1, TUBE_COUNT) or any(len(frame) != images_per_frame for frame in frames):
            raise ValueError(f"{name}: inconsistent images per frame")
        tables.append(position)
        position += len(frames) * images_per_frame * IMAGE_ENTRY.size

    payload = bytearray()
    offsets = {}
    entries = []
    table_bytes = bytearray()
    for (name, fps, frames), table in zip(clips, tables):
        for frame in frames:
            for image in frame:
                if image not in offsets:
                    offsets[image] = position + len(payload)
                    payload += image
                table_bytes += IMAGE_ENTRY.pack(offsets[image], len(image))
        entries.append(CLIP_ENTRY.pack(name.encode("utf-8"), table, len(frames), fps, len(frames[0])))

    body = b"".join(entries) + bytes(table_bytes) + bytes(payload)
    total = HEADER.size + len(body)
    if total > REGION_BYTES:
        raise ValueError(f"The library takes {total} bytes but the region holds {REGION_BYTES}")
    return HEADER.pack(MAGIC, VERSION, total, binascii.crc32(body), len(clips)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--clip", action="append", required=True, metavar="NAME=FILE[:panorama]", dest="clips")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--quality", type=int, default=70)
    args = parser.parse_args()

    clips = []
    try:
        for entry in args.clips:
            name, separator, source = entry.partition("=")
            if not separator:
                parser.error(f"Expected NAME=FILE: {entry}")
            panorama = source.endswith(":panorama")
            path = source[: -len(":panorama")] if panorama else source
            frames = read_frames(path, args.fps)
            encoded = encode_panorama(frames, args.quality) if panorama else encode_common(frames, args.quality)
            clips.append((name, args.fps, encoded))
            print(f"{name}: {len(frames)} frames, {sum(len(i) for frame in encoded for i in frame)} bytes")
        library = build_library(clips)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))

    args.output.write_bytes(library)
    print(f"{args.output}: {len(library)} of {REGION_BYTES} bytes")


if __name__ == "__main__":
    main()
