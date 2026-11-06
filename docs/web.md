# The web page of the clock

Open `http://<clock>/` in a browser, on a phone or a computer on the same network. The page lives in the clock itself, compressed to 16 KB, and everything it does goes through the [HTTP API](api.md). It needs the `web` package, which `firmware/full.yaml` includes. While the clock offers its own network to set up the Wi-Fi, ESPHome's setup page is shown instead.

It speaks English or Spanish, depending on the browser.

## What it does

It is in English, light or dark following your device, and made for a phone first.

- **Clock:** the five slots for clock faces (one built into the firmware and room for more), a countdown, and a few effects.
- **Style:** the theme, which is a set of four animations (startup, connecting, connected and the countdown finale) that the clock always needs and that change together, and a studio to make new faces.
- **Settings:** timezone, a password, a backup of the faces and the theme, the firmware (a new `firmware.ota.bin` can be sent once a password is set), and a few details of the clock.

Changes to faces and the theme wait in the page until you press **Apply**, which sends the whole store to the clock in about ten seconds.

## Making a face

A face is ten images, one per digit, fitted to the 135×240 tubes.

- **From a font.** Pick one of sixteen free fonts from [Fontsource](https://fontsource.org) (OFL or Apache licences) or use a font file of your own, then choose the size, the color of the digits, their glow and the background. A preview shows the digits and the six tubes. The font loads from the internet into your browser, never into the clock, so this needs internet on the device you browse from.
- **From images.** Choose or drop ten images named 0 to 9, such as `0.png` or `digit-7.jpg`. Each is fitted to the tube on black.

The clock reads only baseline JPEG. Browsers write that when asked for a JPEG from a canvas, and the page checks for it.

## Working on the page

The page is `web/index.html`, `style.css` and the scripts `store.js`, `timezones.js` and `app.js`. `tools/build_web.py` joins them into one compressed file that the firmware serves from `components/dekatron_api/web_index.h`. That file is committed, so building the firmware needs no Python, and a test fails when it is out of date.

To work on it without flashing the clock each time:

```bash
python tools/dev_web.py elekstube.local
```

Open <http://127.0.0.1:8000/>. The page is rebuilt on every load, and the calls to `/api/` go to the real clock, so what you do there really changes it.

```bash
python tools/build_web.py                          # after a change, before building the firmware
python -m unittest discover -s tests               # the tools and the page build
node --test tests/store.test.js                    # the store library of the page against the Python one
python tools/check_api.py elekstube.local store.bin  # the API, against a real clock
```

`web/store.js` writes the same bytes as `tools/make_clips.py`, and a test compares them.

## Creating a theme

Themes are made on a page of their own, `site/creator/index.html`, which is meant for the static site of the project (GitHub Pages) and also works from a file. A page served over HTTPS cannot send anything to the clock, which speaks plain HTTP, and the clock has no need to carry the tool, so the page makes a theme file and nothing else: you download it, and on the page of the clock press **Change theme** in the Style tab, then **Apply**.

For each of the four animations the creator takes an animated GIF, WebP or PNG, a video, or a numbered series of images. It fits each picture to the strip that crosses the six tubes (1370 × 240), chooses one palette of 255 colors for all four, stores only what changes between frames, and can give each tube's light the color of its picture. Nothing leaves the computer.

`python tools/build_site.py` builds the page from `site/creator.template.html`, `site/creator-page.js` and the libraries `web/store.js`, `site/scene.js` and `site/creator.js`. The encoder in `site/scene.js` is tested byte for byte against `tools/make_scenes.py` in `tests/scene.test.js`. The Theme card of the clock's page has **Download theme file** to share the theme of the clock.
