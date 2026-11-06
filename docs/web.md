# The web page of the clock

Open `http://<clock>/` in a browser, on a phone or a computer on the same network. The page lives in the clock itself, compressed to about 24 KB, and everything it does goes through the [HTTP API](api.md). It needs the `web` package, which `firmware/full.yaml` includes. While the clock offers its own network to set up the Wi-Fi, ESPHome's setup page is shown instead.

It is in English, light or dark following your device, and made for a phone first. It has three tabs.

- **Clock:** the controls of the clock: displays (on and brightness), lights (on, color, brightness, effect), the display (12 or 24 hours, zero before the hour, digit change, night mode), the face that is shown, a countdown and a few effects.
- **Files:** upload a file, see what is on the clock, and keep a backup.
- **Settings:** timezone, a password, the firmware and its updates, and a few details of the clock.

## Files

The page takes `.bin` files of four kinds, as described in [format.md](format.md), and tells which one a file is by what is inside it, not by its name:

| File | What the clock does |
|---|---|
| Theme | Replaces the theme. The faces stay |
| Face | Adds it in a free slot, or replaces one you choose |
| Backup | Replaces everything: the faces and the theme |
| Firmware | Updates the clock. Needs a password |

Choose the file, or drop it on the card. The page checks it with the rules of the clock (`checkBin` in `web/store.js`, the same as `tools/check_bin.py`) and shows what it is, who made it, its license and anything wrong with it, **before anything changes**. Every dialog has **Download a backup first**. A file that is not valid only offers Close.

A theme or a face goes up as a new store: the page reads the store of the clock, makes the change in it, and sends the whole of it back, which takes about ten seconds. The faces can be removed one by one with the cross. The backup is the store as the clock has it, and uploading it brings it back.

Themes and faces are made on the site of the project, in the browser, and come out as a file to upload here. A page on the site is served over HTTPS and cannot send anything to the clock, which speaks plain HTTP, so the way is always: make the file there, upload it here. The Files tab has links to the gallery and the creator, and [making-bins.md](making-bins.md) has the same by command line or with an AI assistant.

## Updates

The clock never goes to the internet. The page does: when it is open, and at most once every two hours, it asks the site of the project for `versions.json`, which each release publishes, and compares it with the version of the clock (`version` in `/api/state`). If a newer one is there, Settings shows a dot and the notice with an **Update** button. Update downloads the firmware from the site, checks its SHA-256, and sends it to the clock like any other firmware file. The clock restarts, and goes back to the old firmware if the new one does not start. Without internet nothing is shown, and a firmware file can always be uploaded in Files. `web/update.js` holds this, and `tests/update.test.js` tests it.

```json
{"latest": "0.2.0", "date": "2026-11-20", "notes": "What changed",
 "firmware": {"full": {"url": "https://yayitazale.github.io/dekatron/firmware/v0.2.0/dekatron-0.2.0-full.ota.bin", "sha256": "…"}}}
```

## Working on the page

The page is `web/index.html`, `style.css` and the scripts `store.js`, `timezones.js`, `update.js` and `app.js`. `tools/build_web.py` joins them into one compressed file that the firmware serves from `components/dekatron_api/web_index.h`. That file is committed, so building the firmware needs no Python, and a test fails when it is out of date.

To work on it without flashing the clock each time:

```bash
python tools/dev_web.py elekstube.local
```

Open <http://127.0.0.1:8000/>. The page is rebuilt on every load, and the calls to `/api/` go to the real clock, so what you do there really changes it.

```bash
python tools/build_web.py                          # after a change, before building the firmware
python -m unittest discover -s tests               # the tools and the page build
node --test "tests/*.test.js"                      # the libraries of the page against the Python ones
python tools/check_api.py elekstube.local store.bin  # the API, against a real clock
```

`web/store.js` writes the same bytes as `tools/make_clips.py`, and a test compares them.

## The creator

The creator of themes and faces is a page of its own, `site/creator/index.html`, for the static site of the project. `python tools/build_site.py` builds it from `site/creator.template.html`, `site/creator-page.js` and the libraries `web/store.js`, `site/scene.js` and `site/creator.js`. The encoder in `site/scene.js` is tested byte for byte against `tools/make_scenes.py` in `tests/scene.test.js`.
