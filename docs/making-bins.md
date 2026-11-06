# Making themes and faces

This page is for people and for AI assistants that make files for the clock without opening the creator on the site. The result is a `.bin` that the page of the clock takes in Files. It is all done with the tools of this repository, and the checker tells what to fix. For the bytes themselves see [format.md](format.md).

You need Python 3.12 or later with Pillow (`pip install pillow`), and `ffmpeg` on the PATH only if you start from a video.

## Rules in short

- **Face:** ten pictures, one per digit, of **135 × 240** pixels, baseline JPEG. At most four faces besides the built-in one.
- **Theme:** four animations named **`boot`**, **`connecting`**, **`online`** and **`countdown_end`**, in at most **800 KB** in all.
- **Animation:** one strip across the six tubes, **1370 × 240** pixels. The tubes show 135 pixels each and start every 247 pixels, with tube 5 on the left; the 112 pixels between tubes are not shown, so keep important things inside the tubes or let them cross the gaps fast. Black is the color of the tubes at rest.
- **Speed:** 15 frames per second is what the clock draws without dropping frames. Keep the animations short (a few seconds to 15), with few colors and few things that change at once, because only the changes are stored. A black or still background is cheap.
- **Colors:** the four animations of a theme share one palette of 255 colors, so use one style for all of them.
- **Lights:** each of the six LEDs takes the color of the picture over its tube, frame by frame. With `--no-lights` they stay off.
- **Information:** every file carries `--name` (up to 15 bytes), `--author` and `--license`. The gallery of the project takes **`CC-BY-SA-4.0`**. Draw your own pictures: do not use characters, logos or brands you do not own.

## A theme from animations

Get four animated GIFs, WebP or PNG, videos, or folders of numbered pictures (`0001.png`, `0002.png`…), one for each moment.

```bash
python tools/make_theme.py surf.theme.bin --name Surf --author "Your name" --license CC-BY-SA-4.0 \
    --description "Waves and a board" \
    --boot boot.gif --connecting waiting.gif --online connected.gif --countdown_end finale.gif --gallery
```

Pictures of another size are scaled to fill the strip and cropped, so make them 1370 × 240 for what you see to be what you get. The tool prints the size of each animation, and then the problems and warnings of the checker. If it says the theme is too big, use fewer frames, a lower `--fps` or flatter colors.

## A face from a font or from pictures

```bash
python tools/make_face.py neon.face.bin --name Neon --author "Your name" --license CC-BY-SA-4.0 \
    --font MyFont.ttf --color "#00ff66" --gallery

python tools/make_face.py marble.face.bin --name Marble --author "Your name" --license CC-BY-SA-4.0 \
    --images digits/ --gallery
```

With `--font` the digits are drawn in a color with a glow on a background (`--background`, `--size`, `--glow 0` for none). With `--images` the folder has `0.png` to `9.png`, each fitted to the tube on black. Check that the license of the font lets you share a face made from it.

## Checking and fixing

```bash
python tools/check_bin.py surf.theme.bin --gallery
```

Each problem is `rule: what is wrong and how to fix it`. An assistant should run this after every change and stop when there is none. The usual ones:

| Rule | Fix |
|---|---|
| `theme-missing-clip` | Give the theme all four animations |
| `theme-size` | Fewer frames, lower `--fps` or fewer colors and moving things |
| `jpeg-not-baseline`, `jpeg-size`, `jpeg-invalid` | Make the pictures 135 × 240 baseline JPEG again |
| `scene-invalid` | Make the animation again with the tool: do not edit scenes by hand |
| `gallery-license`, `gallery-author` | Set `--license CC-BY-SA-4.0` and `--author` |
| `bad-crc`, `size-mismatch` | The file is damaged or cut: copy or build it again |

## Prompts for an AI assistant

With a terminal, paste something like:

> Read `docs/making-bins.md` in this repository. Make me a theme called "Surf" for the Dekatron clock: waves and a surfboard, in warm colors, four short animations (boot, connecting, online, countdown_end) of 1370 × 240 pixels at 15 fps, drawn by you with Python and Pillow. Build it with `tools/make_theme.py`, license CC-BY-SA-4.0, author "Me". Run `tools/check_bin.py --gallery` and fix every problem until there is none. Give me the path of the `.bin`.

For a face:

> Read `docs/making-bins.md`. Make me a clock face called "Neon" with the font at `./MyFont.ttf` in blue, with `tools/make_face.py`. Check it and give me the path of the `.bin`.

Without a terminal, ask the assistant for the pictures only (four GIFs of 1370 × 240, or ten PNG of 135 × 240) and make the file with the creator on the site, which runs in your browser.

## Putting it on the clock

Open the page of the clock and upload the `.bin`: a theme with **Change theme** in the Style tab, a face as a new face there. (The simplified page will have one Files tab that takes any of them and says what the file is and who made it before it changes anything.) Download a backup first if you want to keep what the clock has.
