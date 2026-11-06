<!-- Title: use Conventional Commits, e.g. "feat(web): add a button to download the backup" -->

## What and why

<!-- What does this change, and what problem does it solve? Link the issue if there is one (Closes #12). -->

## How it was tested

<!-- A real clock (which firmware), `python tools/dev_web.py`, unit tests… -->

## Checklist

- [ ] Commits and PR title follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
- [ ] `python -m unittest discover -s tests` and `node --test "tests/*.test.js"` pass
- [ ] After a change to `web/` or `site/`, `python tools/build_web.py` and `python tools/build_site.py` were run
- [ ] No version bump or `CHANGELOG.md` edit (`scripts/release.py` does that)
- [ ] New code has the SPDX header; themes and faces carry `license: CC-BY-SA-4.0`
