"""Build the store of the clock: its clock faces and clips.

Usage: python tools/make_clips.py store.bin --face Marble=faces/marble --clip alarm=alarm.gif --fps 15

A face is a folder with one image per digit, named 0 to 9 with any image extension. A clip comes
from a GIF, animated WebP or video, and shows the same picture on every tube, or with ":panorama"
one picture across all six, cropped to leave out the gap between screens. Panoramas play at only
3 to 4 fps. Videos need ffmpeg on the PATH. For animations that cross the tubes, see
tools/make_scenes.py.

The store replaces the whole previous one. Serve the file over HTTP and publish
{"url": "http://host/store.bin"} to elekstube/elekstube/store/download, or flash it over USB at
0x2E0000.
"""

import argparse
import binascii
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageSequence

from convert import DIGITS, HEIGHT, WIDTH, encode_jpeg, fit_image, load_face

REGION_BYTES = 0x120000
FLASH_OFFSET = 0x2E0000
MAGIC = b"EKST"
VERSION = 2
NAME_BYTES = 16
KIND_CLIP = 0
KIND_FACE = 1
HEADER = struct.Struct("<4sIIII")  # magic, version, total size, CRC32 of the rest, entry count
# name, table offset, frames, fps, images per frame, kind, scene blobs, reserved
ENTRY = struct.Struct(f"<{NAME_BYTES}sIIHBBHH")
CLIP_ENTRY = ENTRY
IMAGE_ENTRY = struct.Struct("<II")  # offset, size

# Must match TUBE_GAP_PIXELS in animation.h.
TUBE_PITCH = WIDTH + 112
TUBE_COUNT = 6
CANVAS_WIDTH = (TUBE_COUNT - 1) * TUBE_PITCH + WIDTH


@dataclass
class Scene:
    """Encoded by tools/make_scenes.py: only what changes between frames."""

    palette: bytes
    records: list
    blobs: list


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


def build_library(clips, faces=()):
    """Packs the store: faces as (name, [10 JPEG byte strings]) and clips as (name, fps, frames).

    A clip is a list of frames, each a list of JPEG byte strings, or a Scene. Identical images are
    stored once.
    """
    items = [(KIND_FACE, name, 1, [images]) for name, images in faces]
    items += [(KIND_CLIP, name, fps, frames) for name, fps, frames in clips]
    if not items:
        raise ValueError("Expected at least one face or clip")
    for kind in (KIND_FACE, KIND_CLIP):
        names = [name for item_kind, name, _, _ in items if item_kind == kind]
        if len(set(names)) != len(names):
            raise ValueError("Names must be unique")
    if len(items) > 255:
        raise ValueError("The store holds at most 255 faces and clips")

    position = HEADER.size + ENTRY.size * len(items)
    tables = []
    for kind, name, fps, frames in items:
        encoded_name = name.encode("utf-8")
        if not 1 <= len(encoded_name) < NAME_BYTES:
            raise ValueError(f"Invalid name: {name!r}")
        if isinstance(frames, Scene):
            if not frames.records or not 1 <= fps <= 60 or len(frames.palette) != 512 or len(frames.blobs) > 0xFFFF:
                raise ValueError(f"{name}: invalid scene")
            tables.append(position)
            position += (1 + len(frames.records) + len(frames.blobs)) * IMAGE_ENTRY.size
            continue
        if not frames or not 1 <= fps <= 60:
            raise ValueError(f"{name}: invalid frames or fps")
        images_per_frame = len(frames[0])
        allowed = (DIGITS,) if kind == KIND_FACE else (1, TUBE_COUNT)
        if images_per_frame not in allowed or any(len(frame) != images_per_frame for frame in frames):
            raise ValueError(f"{name}: inconsistent images per frame")
        tables.append(position)
        position += len(frames) * images_per_frame * IMAGE_ENTRY.size

    payload = bytearray()
    offsets = {}
    entries = []
    table_bytes = bytearray()

    def add_image(image):
        if image not in offsets:
            offsets[image] = position + len(payload)
            payload.extend(image)
        table_bytes.extend(IMAGE_ENTRY.pack(offsets[image], len(image)))

    for (kind, name, fps, frames), table in zip(items, tables):
        encoded_name = name.encode("utf-8")
        if isinstance(frames, Scene):
            for image in (frames.palette, *frames.records, *frames.blobs):
                add_image(image)
            entries.append(ENTRY.pack(encoded_name, table, len(frames.records), fps, 0, kind, len(frames.blobs), 0))
            continue
        for frame in frames:
            for image in frame:
                add_image(image)
        entries.append(ENTRY.pack(encoded_name, table, len(frames), fps, len(frames[0]), kind, 0, 0))

    body = b"".join(entries) + bytes(table_bytes) + bytes(payload)
    total = HEADER.size + len(body)
    if total > REGION_BYTES:
        raise ValueError(f"The store takes {total} bytes but the region holds {REGION_BYTES}")
    return HEADER.pack(MAGIC, VERSION, total, binascii.crc32(body), len(items)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--face", action="append", default=[], metavar="NAME=FOLDER", dest="faces")
    parser.add_argument("--clip", action="append", default=[], metavar="NAME=FILE[:panorama]", dest="clips")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--quality", type=int, default=70)
    args = parser.parse_args()
    if not args.faces and not args.clips:
        parser.error("Expected at least one --face or --clip")

    faces = []
    clips = []
    try:
        for entry in args.faces:
            name, separator, folder = entry.partition("=")
            if not separator:
                parser.error(f"Expected NAME=FOLDER: {entry}")
            faces.append((name, load_face(folder)))
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
        library = build_library(clips, faces)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))

    args.output.write_bytes(library)
    print(f"{args.output}: {len(library)} of {REGION_BYTES} bytes")


if __name__ == "__main__":
    main()
