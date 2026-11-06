# The creator

The [creator](/creator/index.html){target="_self"} is a page that runs in your browser and **sends nothing anywhere**. It makes the files that the clock takes in **Files**.

## A theme

Give it, for each of the four animations, an animated GIF, WebP or PNG, a video, or a numbered series of images. It fits each picture to the strip that crosses the six tubes (1370 × 240 pixels), chooses one palette of 255 colors for the four, stores only what changes between frames, and can give each tube's light the color of its picture. Then it downloads `name.theme.bin`.

- Few colors and small movements make small files. A theme may take at most 800 KB so that four faces still fit.
- The tubes are 135 pixels wide and start 247 pixels apart, tube 5 on the left, and the 112 pixels between them are never shown.
- Give it your name and a license. The [gallery](/themes) takes `CC-BY-SA-4.0`.

## A face

A face is ten pictures, one per digit, fitted to the 135 × 240 tubes. Open the **Face** tab of the [creator](/creator/index.html#face){target="_self"} and either:

- **From a font:** pick one of sixteen free fonts from [Fontsource](https://fontsource.org) (OFL or Apache licenses) or use a font file of your own, then choose the size, the color of the digits, their glow and the background. A preview shows the digits and the six tubes. The font loads from the internet into your browser, never into the clock.
- **From ten images:** choose or drop ten images named 0 to 9, such as `0.png` or `digit-7.jpg`. Each is fitted to the tube on black.

Then it downloads `name.face.bin`, with your name and license. Upload it in **Files**: it goes into a free slot, or replaces a face you choose. The clock holds up to five faces, one of them built into the firmware.

The same can be done from the [command line](/making-bins) with `tools/make_face.py`.

## Other ways

- [From the command line](/making-bins), with `tools/make_theme.py` and `tools/make_face.py`.
- [With an AI assistant](/create/ai), with a prompt ready to paste.
