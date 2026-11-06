# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cuts a release: syncs the version, writes the changelog, commits and creates the tag.

    python scripts/release.py            # the version comes from the commits
    python scripts/release.py 1.0.0      # a version forced by hand

It does not push, on purpose: the tag is what builds and publishes the release, so publishing must be a
deliberate act. See CONTRIBUTING.md.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Pinned to major 2, so that a change in the format of git-cliff cannot alter the changelog by itself.
GIT_CLIFF = ["npx", "--yes", "git-cliff@2"]
CORE = ROOT / "firmware" / "packages" / "core.yaml"
PROJECT_VERSION = re.compile(r'(project:\s*\n(?:[ \t]+.*\n)*?[ \t]+version:\s*)"[^"]*"')


def run(*args, capture=True):
    shell = sys.platform == "win32" and args[0] == "npx"
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=capture, check=False, shell=shell)
    if result.returncode:
        sys.exit(f"{' '.join(args)} failed:\n{result.stderr or result.stdout}")
    return (result.stdout or "").strip()


def git(*args):
    return run("git", *args)


def check_repository():
    if git("status", "--porcelain"):
        sys.exit("The working tree is not clean. Commit or stash first.")
    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        sys.exit("Releases are cut from main.")
    if "origin" in git("remote").split():
        git("fetch", "origin", "main")
        if git("rev-parse", "HEAD") != git("rev-parse", "origin/main"):
            sys.exit("main differs from origin/main. Pull or push first.")


def next_version(forced):
    if forced:
        if not re.fullmatch(r"\d+\.\d+\.\d+", forced):
            sys.exit(f"{forced} is not a version like 1.2.3")
        return forced
    return run(*GIT_CLIFF, "--bumped-version").removeprefix("v")


def write_versions(version):
    (ROOT / "VERSION").write_text(version + "\n", encoding="utf-8")
    text = CORE.read_text(encoding="utf-8")
    updated, count = PROJECT_VERSION.subn(lambda m: f'{m.group(1)}"{version}"', text)
    if count != 1:
        sys.exit("firmware/packages/core.yaml has no esphome: project: version to update")
    CORE.write_text(updated, encoding="utf-8")


def main():
    forced = sys.argv[1] if len(sys.argv) > 1 else None
    check_repository()
    current = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    version = next_version(forced)
    if git("tag", "--list", f"v{version}"):
        sys.exit(f"The tag v{version} already exists.")
    print(f"{current} -> {version}")

    write_versions(version)
    run(*GIT_CLIFF, "--unreleased", "--tag", f"v{version}", "--prepend", "CHANGELOG.md")
    print(git("diff"))
    if input(f"Commit and tag v{version}? [y/N] ").strip().lower() != "y":
        git("checkout", "--", ".")
        sys.exit("Nothing was committed. The files are back as they were.")

    git("add", "VERSION", "CHANGELOG.md", "firmware/packages/core.yaml")
    git("commit", "-m", f"chore(release): v{version}")
    git("tag", f"v{version}")
    print(f"Done. To publish: git push origin main --follow-tags\n"
          f"To back out: git tag -d v{version} && git reset --hard HEAD~1")


if __name__ == "__main__":
    main()
