# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the pages of the static site, which GitHub Pages serves.

Usage: python tools/build_site.py

site/creator.template.html becomes docs/public/creator/index.html with the style of the clock's page and the
scripts that make a theme. The page is one file that works from a folder as well as from the web, and the
site of the project (docs/, built with VitePress) serves it as it is. The gallery of themes is built apart,
by tools/build_themes.py, into docs/public/themes/.
"""

import sys
from pathlib import Path

from build_web import squeeze_css

ROOT = Path(__file__).parent.parent
SITE = ROOT / "site"
SCRIPTS = (ROOT / "web" / "store.js", SITE / "scene.js", SITE / "creator.js", SITE / "creator-page.js")
TEMPLATE = SITE / "creator.template.html"
OUTPUT = ROOT / "docs" / "public" / "creator" / "index.html"


def build_page(template, scripts, extra_css=()):
    page = template.read_text(encoding="utf-8")
    css = squeeze_css("\n".join(path.read_text(encoding="utf-8") for path in (ROOT / "web" / "style.css", *extra_css)))
    script = "\n".join(path.read_text(encoding="utf-8") for path in scripts)
    if "</script" in script:
        sys.exit("A script contains </script")
    return page.replace("/*STYLE*/", css).replace("/*SCRIPT*/", script)


def build_creator():
    return build_page(TEMPLATE, SCRIPTS)


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(build_creator(), encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
