# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Files for the clock, good and bad, for the tests of tools/check_bin.py and of checkBin in web/store.js.

Run it to print them as JSON: {name: {"hex": ..., "gallery": bool, "type": ..., "problems": [...], "warnings": [...]}},
with what check_bin.py says about each. The test of the page compares its own answer with that.
"""

import binascii
import io
import json
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

import check_bin  # noqa: E402
import make_clips  # noqa: E402
from convert import HEIGHT, WIDTH  # noqa: E402


def jpeg(width=WIDTH, height=HEIGHT, **options):
    buffer = io.BytesIO()
    Image.new("RGB", (width, height), (200, 30, 30)).save(buffer, "JPEG", **options)
    return buffer.getvalue()


GOOD = jpeg(quality=80, subsampling="4:2:0", progressive=False)
INFO = {"author": "Someone", "license": "CC-BY-SA-4.0"}


def scene(patch=None):
    record = struct.pack("<H", 0) if patch is None else struct.pack("<H", 1) + patch
    return make_clips.Scene(bytes(512), [record], [b"\x00"] if patch else [])


def theme(roles=make_clips.THEME_ROLES, info=INFO, extra=(), patch=None):
    clips = [(name, 15, scene(patch)) for name in (*roles, *extra)]
    return make_clips.build_library(clips, theme=("Test", info))


def face(images, info=INFO, name="Neon"):
    return make_clips.build_library([], [(name, images, info) if info else (name, images)])


def firmware(size=0x40000, chip=0):
    return b"\xe9" + bytes(11) + struct.pack("<H", chip) + bytes(size - 14)


def damaged(library, at):
    data = bytearray(library)
    data[at] ^= 0xFF
    return bytes(data)


def cases():
    digits = [GOOD] * 10
    good_theme = theme()
    return {
        "good-face": (face(digits), False),
        "good-face-gallery": (face(digits), True),
        "face-without-info": (face(digits, info=None), False),
        "face-another-license": (face(digits, info={"author": "A", "license": "MIT"}), True),
        "face-progressive": (face([jpeg(progressive=True)] + [GOOD] * 9), False),
        "face-wrong-size": (face([jpeg(100, 100, quality=80)] + [GOOD] * 9), False),
        "face-not-jpeg": (face([b"hello"] + [GOOD] * 9), False),
        "face-444": (face([jpeg(quality=80, subsampling="4:4:4")] + [GOOD] * 9), False),
        "face-bad-info": (face(digits, info={"a": "x"}).replace(b'{"a":"x"}', b"[1,2,3,4]"), False),
        "good-theme": (good_theme, False),
        "good-theme-gallery": (good_theme, True),
        "theme-missing-clip": (theme(roles=("boot", "online", "countdown_end")), False),
        "theme-extra-clip": (theme(extra=("other",)), False),
        "theme-no-info": (theme(info={}), True),
        "theme-bad-patch": (theme(patch=bytes([9, 0, 0, 5, 5, 0, 0, 0])), False),
        "theme-good-patch": (theme(patch=bytes([0, 0, 0, 5, 5, 0, 0, 0])), False),
        "backup": (make_clips.build_library([("clip", 15, [[GOOD]])], [("Neon", digits)]), False),
        "backup-two-faces": (make_clips.build_library([], [("Neon", digits), ("Marble", digits)]), False),
        "clip-bad-fps": (make_clips.build_library([("clip", 15, [[GOOD]])], []).replace(b"\x0f\x00\x01\x00", b"\x00\x00\x01\x00"), False),
        "not-a-store": (b"EKSX" + bytes(100), False),
        "too-short": (b"EKS", False),
        "bad-crc": (damaged(good_theme, len(good_theme) - 1), False),
        "bad-version": (damaged(good_theme, 4), False),
        "cut-short": (good_theme[:-5], False),
        "firmware": (firmware(), False),
        "firmware-other-chip": (firmware(chip=5), False),
        "firmware-too-small": (firmware(size=1000), False),
        "firmware-too-big": (firmware(size=0x170000), False),
    }


def fix_crc(library):
    """Rewrites the checksum and the size of a library that was edited by hand."""
    data = bytearray(library)
    struct.pack_into("<I", data, 12, binascii.crc32(bytes(data[make_clips.HEADER.size:])))
    return bytes(data)


def answers():
    out = {}
    for name, (data, gallery) in cases().items():
        if name in ("clip-bad-fps", "face-bad-info"):
            data = fix_crc(data)
        report = check_bin.check(data, gallery)
        out[name] = {"hex": data.hex(), "gallery": gallery, "type": report.type,
                     "problems": [rule for rule, _ in report.problems], "warnings": [rule for rule, _ in report.warnings]}
    return out


if __name__ == "__main__":
    sys.stdout.write(json.dumps(answers()))
