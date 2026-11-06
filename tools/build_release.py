# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble the files of a release from what the build made.

Usage: python tools/build_release.py VERSION --firmware DIR --themes DIR --out DIR --site DIR [--date YYYY-MM-DD]

--firmware has a folder for each profile (clock/, full/) with the firmware.factory.bin and firmware.ota.bin that
ESPHome built. --themes is the folder that tools/build_themes.py made. The files for the release page of GitHub go
to --out:

  dekatron-VERSION-PROFILE.factory.bin   the first installation, to write at address 0 of the flash: the bootloader,
                                          the partitions, the app, and the store with the default theme
  dekatron-VERSION-PROFILE.ota.bin       to update a clock that already runs Dekatron
  THEME.theme.bin                        each theme of the gallery
  SHA256SUMS

and to --site, the folder that the website is built from: versions.json, which the page of the clock reads to tell
that there is a newer firmware, and a copy of the .ota.bin files, because the downloads of the release page of
GitHub do not allow a browser to read them from another site (see web/update.js). Every file is checked with
tools/check_bin.py first, and nothing is written if one is wrong.
"""

import argparse
import hashlib
import json
import re
import shutil
import sys
from datetime import date
from pathlib import Path

import check_bin
from make_clips import FLASH_OFFSET, REGION_BYTES

PROFILES = ("clock", "full")
FLASH_BYTES = 0x400000
DEFAULT_THEME = "space"
SITE_URL = "https://yayitazale.github.io/dekatron/"


def changelog_section(text, version):
    """The notes of one version: what CHANGELOG.md has under its heading, without the heading."""
    match = re.search(rf"^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## \[|\Z)", text, re.S | re.M)
    return match.group(1).strip() if match else ""


def factory_image(app, store):
    """The app image of ESPHome, padded with the erased state of the flash up to the store, and the store after it."""
    if len(app) > FLASH_OFFSET:
        raise ValueError(f"The factory image takes {len(app)} bytes and the store starts at {FLASH_OFFSET}")
    if len(store) > REGION_BYTES:
        raise ValueError(f"The store takes {len(store)} bytes and its region holds {REGION_BYTES}")
    image = app + b"\xff" * (FLASH_OFFSET - len(app)) + store
    if len(image) > FLASH_BYTES:
        raise ValueError("The image does not fit in the 4 MB of the flash")
    return image


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def build(version, firmware, themes, out, site, today, changelog=""):
    # Everything is read and checked before anything is written.
    gathered = {}
    for profile in PROFILES:
        ota = (firmware / profile / "firmware.ota.bin").read_bytes()
        factory = (firmware / profile / "firmware.factory.bin").read_bytes()
        report = check_bin.check(ota)
        if report.type != "firmware" or not report.ok:
            raise ValueError(f"{profile}: firmware.ota.bin is not a firmware for the clock: {report.problems}")
        if factory[0x20000:0x20001] != b"\xe9":
            raise ValueError(f"{profile}: firmware.factory.bin does not have the app at 0x20000")
        gathered[profile] = (ota, factory)

    theme_files = sorted(themes.glob("*.theme.bin"))
    if not any(path.name == f"{DEFAULT_THEME}.theme.bin" for path in theme_files):
        raise ValueError(f"The default theme {DEFAULT_THEME} is not in {themes}")
    for path in theme_files:
        report = check_bin.check(path.read_bytes(), gallery=True)
        if not report.ok:
            raise ValueError(f"{path.name} does not pass the checker: {report.problems}")
    store = (themes / f"{DEFAULT_THEME}.theme.bin").read_bytes()

    out.mkdir(parents=True, exist_ok=True)
    release_dir = site / "firmware" / f"v{version}"
    release_dir.mkdir(parents=True, exist_ok=True)
    entries = {}
    for profile, (ota, factory) in gathered.items():
        (out / f"dekatron-{version}-{profile}.factory.bin").write_bytes(factory_image(factory, store))
        (out / f"dekatron-{version}-{profile}.ota.bin").write_bytes(ota)
        (release_dir / f"dekatron-{version}-{profile}.ota.bin").write_bytes(ota)
        entries[profile] = {"url": f"{SITE_URL}firmware/v{version}/dekatron-{version}-{profile}.ota.bin", "sha256": sha256(ota)}
    for path in theme_files:
        shutil.copy2(path, out / path.name)

    sums = "".join(f"{sha256(path.read_bytes())}  {path.name}\n" for path in sorted(out.iterdir()) if path.name != "SHA256SUMS")
    (out / "SHA256SUMS").write_text(sums, encoding="utf-8")
    versions = {"latest": version, "date": today, "notes": changelog_section(changelog, version), "firmware": entries}
    (site / "versions.json").write_text(json.dumps(versions, indent=2) + "\n", encoding="utf-8")
    return versions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="such as 0.1.0, without the v")
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--themes", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--site", type=Path, required=True)
    parser.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--date", default=date.today().isoformat())
    parser.add_argument("--notes", type=Path, help="write the notes of this version to a file, for the release page")
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("the version is like 1.2.3")
    changelog = args.changelog.read_text(encoding="utf-8") if args.changelog.is_file() else ""
    try:
        versions = build(args.version, args.firmware, args.themes, args.out, args.site, args.date, changelog)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
    if args.notes:
        args.notes.write_text(versions["notes"] or f"Dekatron {args.version}\n", encoding="utf-8")
    print(f"{args.version}: {len(list(args.out.iterdir()))} files in {args.out}, versions.json in {args.site}")


if __name__ == "__main__":
    main()
