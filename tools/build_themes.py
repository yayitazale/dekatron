# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the themes of the gallery: a .theme.bin and a preview for each, and the index the site reads.

Usage: python tools/build_themes.py [--themes themes] [--out docs/public/themes] [ID ...]

Each folder themes/ID/ has a theme.json. Either {"builder": "make_scenes"}, for the theme that is drawn by
code, or a theme made from pictures:

  {"name": "Surf", "author": "Someone", "license": "CC-BY-SA-4.0", "description": "Waves", "fps": 15,
   "lights": true, "sources": {"boot": "boot.gif", "connecting": "...", "online": "...", "countdown_end": "..."}}

where the sources are files or folders inside themes/ID/. Every theme is checked with tools/check_bin.py
--gallery, and the build fails if it does not pass. The result goes to --out: ID.theme.bin, ID.gif (a
preview of the six tubes) and index.json, which the gallery of the site reads. The release publishes them.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

from PIL import Image

import check_bin
from make_clips import THEME_ROLES
from make_theme import build_theme_library

ROOT = Path(__file__).parent.parent
PREVIEW_SIZE = (685, 120)  # half of the strip that crosses the six tubes
PREVIEW_FRAMES = 60  # at most, taken evenly from the whole startup animation


def preview(canvases, fps, path):
    step = max(1, len(canvases) // PREVIEW_FRAMES)
    frames = [canvas.resize(PREVIEW_SIZE, Image.LANCZOS).quantize(colors=64, method=Image.MEDIANCUT) for canvas in canvases[::step]]
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=1000 * step // fps, loop=0, optimize=True)


def build_one(folder, out, report):
    config = json.loads((folder / "theme.json").read_text(encoding="utf-8"))
    if config.get("builder") == "make_scenes":
        import make_scenes
        library, made = make_scenes.build_space(report)
        fps = 15
        canvases = made[0][2]
    elif "sources" in config:
        missing = [role for role in THEME_ROLES if role not in config["sources"]]
        if missing:
            raise ValueError(f"{folder.name}: no source for {', '.join(missing)}")
        info = {key: config[key] for key in ("author", "license", "description") if config.get(key)}
        fps = config.get("fps", 15)
        paths = {role: folder / config["sources"][role] for role in THEME_ROLES}
        library, made = build_theme_library(config["name"], info, paths, fps, config.get("lights", True), report)
        canvases = made[0][1]
    else:
        raise ValueError(f"{folder.name}: theme.json needs a builder or sources")

    result = check_bin.check(library, gallery=True)
    if not result.ok:
        raise ValueError(f"{folder.name} does not pass the checker:\n" + "\n".join(f"  {rule}: {message}" for rule, message in result.problems))
    (out / f"{folder.name}.theme.bin").write_bytes(library)
    preview(canvases, fps, out / f"{folder.name}.gif")
    return {
        "id": folder.name, "name": result.name, "author": result.info.get("author", ""), "license": result.info.get("license", ""),
        "description": result.info.get("description", ""), "file": f"{folder.name}.theme.bin", "preview": f"{folder.name}.gif",
        "bytes": len(library), "sha256": hashlib.sha256(library).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ids", nargs="*", help="only these themes")
    parser.add_argument("--themes", type=Path, default=ROOT / "themes")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "public" / "themes")
    args = parser.parse_args()

    folders = sorted(path.parent for path in args.themes.glob("*/theme.json"))
    if args.ids:
        folders = [folder for folder in folders if folder.name in args.ids]
    if not folders:
        sys.exit("No themes found")
    args.out.mkdir(parents=True, exist_ok=True)
    index = []
    try:
        for folder in folders:
            print(f"== {folder.name}")
            index.append(build_one(folder, args.out, print))
    except (ValueError, OSError) as error:
        sys.exit(str(error))
    (args.out / "index.json").write_text(json.dumps({"themes": index}, indent=2) + "\n", encoding="utf-8")
    print(f"{len(index)} themes in {args.out}")


if __name__ == "__main__":
    main()
