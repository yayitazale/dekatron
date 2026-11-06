# Contributing to Dekatron

Thanks for helping out! Bug reports, ideas, themes and pull requests are all welcome.

## Reporting bugs and ideas

Open an issue on GitHub: https://github.com/yayitazale/dekatron/issues

For bugs, include the firmware version (shown on the clock's page in Settings, or `version` in `GET /api/state`), the profile (`clock` or `full`) and what the clock did. A log taken over USB helps (`esphome logs`).

## Pull requests

1. **Fork** the project and create a branch from `main`, named after the change: `feat/theme-surf`, `fix/upload-timeout`, `docs/install-guide`…
2. Keep each PR focused on one thing. Small PRs get reviewed and merged much faster.
3. Use **[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)** for commit messages and for the PR title, in English and in the imperative mood:

   ```
   feat(web): add a button to download the backup
   fix(store): refuse a theme with a missing clip
   ```

   The scope is optional. The ones in use are `firmware`, `store`, `api`, `web`, `site`, `creator`, `install`, `themes`, `scenes`, `clips` and `effects`. The changelog is generated from these prefixes, and commits that do not follow the format are left out of it.

   | Prefix | Changelog section | Bumps the version |
   | --- | --- | --- |
   | `feat` | Added | minor (0.3.0 → 0.4.0) |
   | `fix`, `perf`, `refactor`, `revert` | Fixed, Performance, Changed, Reverted | patch |
   | `docs` | Documentation | no |
   | `chore`, `build`, `ci`, `test`, `style` | Maintenance | no |

   While the project is 0.x a breaking change (`feat(api)!: …` or a `BREAKING CHANGE:` line) bumps the minor number too. The jump to 1.0.0 is made by hand.
4. Check that the tests pass (see below).
5. Open a **pull request against `main`**. Describe what changes and why, and how you tested it. The checks must be green before it is merged.

Please **don't** bump version numbers or edit `CHANGELOG.md`: releases are cut by the maintainer with `scripts/release.py`, which does both.

## Branches and releases

`main` is the only long-lived branch. Every change comes from a branch cut from `main` and goes back through a pull request. A merge publishes nothing.

A release is a tag `vX.Y.Z` on `main`, and nothing else publishes: the tag is what builds the firmware images, the themes and the site. The maintainer decides when. From `main`, with everything merged and no pending changes:

```bash
python scripts/release.py          # the next version comes from the commits
python scripts/release.py 1.0.0    # or a number forced by hand
```

The script checks that the tree is clean and that you are on `main`, works out the version with [git-cliff](https://git-cliff.org) (`cliff.toml`, run through `npx git-cliff@2`), updates `VERSION` and the version of the firmware, prepends the new section to `CHANGELOG.md`, **shows the diff and waits for confirmation**, and creates the `chore(release): vX.Y.Z` commit and the tag. It does not push: publishing is a deliberate act.

```bash
git push origin main --follow-tags
```

To back out before pushing: `git tag -d vX.Y.Z && git reset --hard HEAD~1`.

### What the tag publishes

`.github/workflows/release.yml` runs when the tag arrives, and publishes nothing if a step fails:

1. **check-version** compares the tag with `VERSION`.
2. **test** runs the tests of Python and Node, and **firmware** compiles `clock` and `full`, with the same limit on the size of the app as CI.
3. **release** builds the themes of the gallery (`tools/build_themes.py`) and assembles the files with `tools/build_release.py`: for each profile `dekatron-X.Y.Z-PROFILE.factory.bin` (the first installation, written at address 0, with the default theme in the store) and `.ota.bin` (to update), each theme, and `SHA256SUMS`. It then creates the release page of GitHub with the notes of that version from `CHANGELOG.md`.
4. **pages** builds the website with the gallery, the creator, `versions.json` (what the page of the clock reads to tell that there is a newer firmware) and a copy of the `.ota.bin` files, and deploys it. The firmware is copied to the site because a browser cannot read the downloads of the release page from another site. For this, Pages has to be set to **GitHub Actions** in the settings of the repository.

### The table of partitions is frozen

`firmware/partitions.csv` cannot be changed on a clock over the air, so a test (`tests/test_partitions.py`) fails if it changes. The store lives at `0x2E0000`, 1152 KB, and the two slots of the app are 1408 KB each.

## Local development

Needs Python 3.12 or later with Pillow, and Node 22.

```bash
python -m unittest discover -s tests   # the tools and the page build
node --test "tests/*.test.js"          # the store and scene libraries of the pages, against the Python ones
python tools/build_web.py              # after a change in web/, before building the firmware
python tools/build_site.py             # after a change in site/ or web/store.js: the creator page
python tools/build_themes.py           # the gallery of themes, into docs/public/themes/
(cd docs && npm ci && npm run dev)     # the website, with its own server
python tools/dev_web.py elekstube.local   # the clock's page on http://127.0.0.1:8000, talking to a real clock
```

To build the firmware, install [ESPHome](https://esphome.io), copy `firmware/secrets.example.yaml` to `firmware/secrets.yaml` and run `esphome run firmware/my-clock.yaml`. `docs/web.md` and `docs/api.md` describe the page and the API.

## Licenses

- **Code** is under the [GNU GPL v3 or later](LICENSE). Add this header to new source files, in the comment style of the language:

  ```
  Copyright (c) 2026 Joseba Egia and Dekatron contributors
  SPDX-License-Identifier: GPL-3.0-or-later
  ```

- **Themes and clock faces** (the art) are under [CC BY-SA 4.0](themes/LICENSE). Each one says its author and `license` in its own information block, and the gallery only takes those with `CC-BY-SA-4.0`. Draw your own pictures: do not copy characters, logos or brands.

By contributing you agree that your code is licensed under the GPL v3 or later and your themes under CC BY-SA 4.0.
