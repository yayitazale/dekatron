# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a file for the clock, with the rules the clock itself applies, and say what is wrong.

Usage: python tools/check_bin.py FILE [FILE ...] [--gallery]

It takes a theme (*.theme.bin), a clock face (*.face.bin), a backup of the store (a store with
anything else in it) or a firmware (*.ota.bin), and tells which one it is by what is inside, not by the
name. Every problem is a line `rule: what is wrong and how to fix it`; the exit code is 1 when there is
one. Warnings do not fail. With --gallery the file must also be fit for the gallery of the project: author
and license (CC-BY-SA-4.0) in its information. web/store.js has the same checks (checkBin) for the pages.
"""

import argparse
import binascii
import json
import struct
import sys
from pathlib import Path

from make_clips import (ENTRY, FACE_INFO_IMAGES, HEADER, IMAGE_ENTRY, KIND_CLIP, KIND_FACE, KIND_THEME, LED_BYTES,
                        MAGIC, MAX_FACES, NAME_BYTES, REGION_BYTES, THEME_ROLES, TUBE_COUNT, VERSION)
from convert import DIGITS, HEIGHT, WIDTH

THEME_BYTES = 800 * 1024  # what a theme may take of the store, so that the faces of the user still fit
APP_BYTES = 0x160000  # the partition of the app
GALLERY_LICENSE = "CC-BY-SA-4.0"
PATCH = struct.Struct("<BBBBBxH")  # tube, x, y, width, height, blob
MAX_REPORTED = 3  # of the same rule in one entry, so that a broken clip does not flood the answer


class Report:
    def __init__(self):
        self.type = "unknown"
        self.name = ""
        self.info = {}
        self.problems = []  # (rule, message)
        self.warnings = []

    def problem(self, rule, message):
        self.problems.append((rule, message))

    def warn(self, rule, message):
        self.warnings.append((rule, message))

    @property
    def ok(self):
        return not self.problems


def jpeg_info(data):
    """The size and kind of a JPEG, or a string that says why it is not one the clock can read."""
    if data[:2] != b"\xff\xd8":
        return "not a JPEG: it does not start with the JPEG marker"
    i = 2
    while i + 4 <= len(data):
        if data[i] != 0xFF:
            return "damaged JPEG: a marker is missing"
        marker = data[i + 1]
        if marker == 0xFF:
            i += 1
            continue
        if marker == 0x01 or 0xD0 <= marker <= 0xD8:
            i += 2
            continue
        length = struct.unpack_from(">H", data, i + 2)[0]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            if i + 10 > len(data):
                return "damaged JPEG: it ends inside its header"
            height, width = struct.unpack_from(">HH", data, i + 5)
            components = data[i + 9]
            sampling = [data[i + 11 + 3 * n] for n in range(components) if i + 11 + 3 * n < len(data)]
            return {"baseline": marker == 0xC0, "width": width, "height": height,
                    "subsampling_420": components == 3 and sampling[:3] == [0x22, 0x11, 0x11]}
        if marker == 0xDA:
            return "damaged JPEG: the picture starts before its size"
        i += 2 + length
    return "damaged JPEG: it has no size"


def parse_store(data, report):
    """The entries of a store, or None after saying in the report why it cannot be read."""
    if len(data) < HEADER.size:
        report.problem("not-a-store", "The file is too short to be a store.")
        return None
    magic, version, total, crc, count = HEADER.unpack_from(data)
    if magic != MAGIC:
        report.problem("not-a-store", "It is not a store, a firmware or a file for the clock: it does not start with EKST.")
        return None
    if version != VERSION:
        report.problem("version", f"The store is version {version} and the clock reads version {VERSION}. Make it again with the tools of this project.")
        return None
    if total != len(data):
        report.problem("size-mismatch", f"The header says {total} bytes and the file has {len(data)}. The file is cut short or has extra bytes: copy it again.")
        return None
    if binascii.crc32(data[HEADER.size:]) != crc:
        report.problem("bad-crc", "The checksum does not match: the file is damaged. Copy or build it again.")
        return None
    if not 1 <= count <= 255 or HEADER.size + count * ENTRY.size > total:
        report.problem("malformed", "The number of entries is not valid.")
        return None
    entries = []
    for index in range(count):
        raw_name, table, frames, fps, per_frame, kind, blobs, flags = ENTRY.unpack_from(data, HEADER.size + index * ENTRY.size)
        name = raw_name.split(b"\0", 1)[0].decode("utf-8", "replace")
        images = 1 + frames + blobs + (flags & 1) if per_frame == 0 else frames * per_frame
        if table + images * IMAGE_ENTRY.size > total:
            report.problem("malformed", f"{name}: its table of images goes past the end of the file.")
            return None
        locations = [IMAGE_ENTRY.unpack_from(data, table + n * IMAGE_ENTRY.size) for n in range(images)]
        if any(offset + size > total for offset, size in locations):
            report.problem("malformed", f"{name}: an image goes past the end of the file.")
            return None
        entries.append({"name": name, "frames": frames, "fps": fps, "per_frame": per_frame, "kind": kind,
                        "blobs": blobs, "flags": flags, "locations": locations,
                        "images": [data[offset:offset + size] for offset, size in locations]})
    return entries


def check_scene(entry, report):
    name = entry["name"]
    images = entry["images"]
    if len(images[0]) != 512:
        report.problem("scene-invalid", f"{name}: the palette must take 512 bytes (256 colors of 16 bits) and takes {len(images[0])}.")
        return
    frames = entry["frames"]
    for frame in range(frames):
        record = images[1 + frame]
        if len(record) < 2:
            report.problem("scene-invalid", f"{name}: frame {frame} has no record.")
            return
        count = struct.unpack_from("<H", record)[0]
        if len(record) != 2 + count * PATCH.size:
            report.problem("scene-invalid", f"{name}: frame {frame} says {count} patches but its record has another size.")
            return
        for n in range(count):
            tube, x, y, width, height, blob = PATCH.unpack_from(record, 2 + n * PATCH.size)
            if tube >= TUBE_COUNT or width == 0 or height == 0 or x + width > WIDTH or y + height > HEIGHT or blob >= entry["blobs"]:
                report.problem("scene-invalid", f"{name}: a patch of frame {frame} is outside its tube or points to a missing blob.")
                return
    if entry["flags"] & 1 and len(images[1 + frames + entry["blobs"]]) != frames * LED_BYTES:
        report.problem("scene-invalid", f"{name}: the lights need {LED_BYTES} bytes for each frame ({frames * LED_BYTES}).")


def check_jpegs(entry, images, report):
    reported = 0
    for number, image in enumerate(images):
        info = jpeg_info(image)
        where = f"{entry['name']}, image {number}"
        if isinstance(info, str):
            rule, message = "jpeg-invalid", f"{where}: {info}."
        elif not info["baseline"]:
            rule, message = "jpeg-not-baseline", f"{where}: the clock reads only baseline JPEG, and this one is progressive or of another kind. Save it again as a standard (baseline) JPEG."
        elif (info["width"], info["height"]) != (WIDTH, HEIGHT):
            rule, message = "jpeg-size", f"{where}: it is {info['width']}x{info['height']} and the tubes are {WIDTH}x{HEIGHT}."
        else:
            if not info["subsampling_420"]:
                report.warn("jpeg-subsampling", f"{where}: it is not 4:2:0, which is what the tools write and the clock has been tried with.")
            continue
        report.problem(rule, message)
        reported += 1
        if reported >= MAX_REPORTED:
            return


def read_info(entry, report, what):
    try:
        info = json.loads(entry["images"][-1].decode("utf-8"))
        if not isinstance(info, dict):
            raise ValueError
        return info
    except (ValueError, UnicodeDecodeError):
        report.warn("info-invalid", f"{what}: its information is not a JSON object.")
        return {}


def classify(entries):
    faces = [e for e in entries if e["kind"] == KIND_FACE]
    themes = [e for e in entries if e["kind"] == KIND_THEME]
    clips = [e for e in entries if e["kind"] == KIND_CLIP]
    if len(faces) == 1 and not themes and not clips:
        return "face"
    if len(themes) == 1 and not faces:
        return "theme"
    return "backup"


def check_store(data, gallery=False):
    report = Report()
    entries = parse_store(data, report)
    if entries is None:
        return report
    report.type = classify(entries)
    faces = [e for e in entries if e["kind"] == KIND_FACE]
    themes = [e for e in entries if e["kind"] == KIND_THEME]
    clips = [e for e in entries if e["kind"] == KIND_CLIP]

    if len(faces) > MAX_FACES:
        report.problem("too-many-faces", f"It has {len(faces)} faces and the store holds {MAX_FACES} besides the one in the firmware.")
    if len(themes) > 1:
        report.problem("several-themes", "It has more than one theme and the store holds one.")
    for kind in (KIND_FACE, KIND_CLIP):
        names = [e["name"] for e in entries if e["kind"] == kind]
        if len(set(names)) != len(names):
            report.problem("duplicate-name", "Two faces or two clips have the same name. Names must be unique.")
    for entry in entries:
        name = entry["name"]
        if not 1 <= len(name.encode("utf-8")) < NAME_BYTES:
            report.problem("bad-name", f"{name!r}: a name takes 1 to {NAME_BYTES - 1} bytes.")

    for face in faces:
        if face["frames"] != 1 or face["per_frame"] not in (DIGITS, FACE_INFO_IMAGES):
            report.problem("face-images", f"{face['name']}: a face is ten images, one per digit, and may have an eleventh with its information.")
            continue
        check_jpegs(face, face["images"][:DIGITS], report)
        if face["per_frame"] == FACE_INFO_IMAGES:
            info = read_info(face, report, face["name"])
        else:
            info = {}
        if len(faces) == 1 and report.type == "face":
            report.name, report.info = face["name"], info

    for clip in clips:
        if clip["fps"] == 0 or clip["fps"] > 60 or clip["frames"] == 0:
            report.problem("clip-shape", f"{clip['name']}: it needs 1 to 60 frames per second and at least one frame.")
        elif clip["per_frame"] == 0:
            check_scene(clip, report)
        elif clip["per_frame"] in (1, TUBE_COUNT):
            check_jpegs(clip, clip["images"], report)
        else:
            report.problem("clip-shape", f"{clip['name']}: a frame is one image for every tube or one for each of the six, and this one has {clip['per_frame']}.")

    if report.type == "theme" and len(themes) == 1:
        theme = themes[0]
        report.name = theme["name"]
        report.info = read_info(theme, report, theme["name"])
        names = [clip["name"] for clip in clips]
        for role in THEME_ROLES:
            if role not in names:
                report.problem("theme-missing-clip", f"The theme has no animation named {role}. A theme has exactly boot, connecting, online and countdown_end.")
        for extra in sorted(set(names) - set(THEME_ROLES)):
            report.problem("theme-extra-clip", f"The animation {extra} is not part of a theme, which has only boot, connecting, online and countdown_end.")
        unique = {location for entry in [theme, *clips] for location in entry["locations"]}
        used = sum(size for _, size in unique)
        if used > THEME_BYTES:
            report.problem("theme-size", f"The theme takes {used} bytes and the limit is {THEME_BYTES}. Use fewer frames, a lower frame rate or fewer colors.")
    if len(data) > REGION_BYTES:
        report.problem("store-size", f"The file takes {len(data)} bytes and the store of the clock holds {REGION_BYTES}.")

    if report.type in ("theme", "face"):
        if not report.info.get("author"):
            report.warn("info-author", "No author in its information.")
        if not report.info.get("license"):
            report.warn("info-license", "No license in its information.")
        if gallery:
            if not report.info.get("author"):
                report.problem("gallery-author", "The gallery needs the author in the information.")
            if report.info.get("license") != GALLERY_LICENSE:
                report.problem("gallery-license", f'The gallery takes only "license": "{GALLERY_LICENSE}".')
    return report


def check_firmware(data):
    report = Report()
    report.type = "firmware"
    if len(data) < 0x10000:
        report.problem("firmware-size", "The file is too small to be a firmware.")
    elif len(data) > APP_BYTES:
        report.problem("firmware-size", f"The firmware takes {len(data)} bytes and the app partition holds {APP_BYTES}.")
    if len(data) >= 14 and struct.unpack_from("<H", data, 12)[0] != 0:
        report.problem("firmware-chip", "This firmware is for another chip, not the ESP32 of the clock.")
    return report


def check(data, gallery=False):
    """A Report for the bytes of a file."""
    if data[:4] == MAGIC or data[:1] != b"\xe9":
        return check_store(data, gallery)
    return check_firmware(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--gallery", action="store_true", help="also check what the gallery of the project asks")
    args = parser.parse_args()
    failed = False
    for path in args.files:
        report = check(path.read_bytes(), args.gallery)
        label = f"{path}: {report.type}" + (f" {report.name!r}" if report.name else "")
        print(f"{label} - {'ok' if report.ok else 'NOT OK'}")
        for rule, message in report.problems:
            print(f"  problem  {rule}: {message}")
        for rule, message in report.warnings:
            print(f"  warning  {rule}: {message}")
        failed = failed or not report.ok
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
