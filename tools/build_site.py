# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the pages of the static site, which GitHub Pages serves.

Usage: python tools/build_site.py

site/creator.template.html becomes site/creator/index.html with the style of the clock's page and the scripts
that make a theme. The page is one file that works from a folder as well as from the web.
"""

import sys
from pathlib import Path

from build_web import squeeze_css

ROOT = Path(__file__).parent.parent
SCRIPTS = (ROOT / "web" / "store.js", ROOT / "site" / "scene.js", ROOT / "site" / "creator.js", ROOT / "site" / "creator-page.js")
TEMPLATE = ROOT / "site" / "creator.template.html"
OUTPUT = ROOT / "site" / "creator" / "index.html"


def build_creator():
    page = TEMPLATE.read_text(encoding="utf-8")
    css = squeeze_css((ROOT / "web" / "style.css").read_text(encoding="utf-8"))
    script = "\n".join(path.read_text(encoding="utf-8") for path in SCRIPTS)
    if "</script" in script:
        sys.exit("A script contains </script")
    return page.replace("/*STYLE*/", css).replace("/*SCRIPT*/", script)


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(build_creator(), encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
