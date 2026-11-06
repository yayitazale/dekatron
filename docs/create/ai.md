# Create with an AI

The tools of the repository make the same files from the command line and **check them against the rules of the clock**, so an assistant can make them for you and fix its own mistakes. The assistant needs to read [From the command line](/making-bins).

## With an assistant that has a terminal

Paste this in Claude Code or similar, in a copy of the repository, and change the idea:

```text
Read docs/making-bins.md in this repository. Make me a theme called "Surf" for the Dekatron clock: waves and a surfboard, in warm colors, four short animations (boot, connecting, online, countdown_end) of 1370 x 240 pixels at 15 fps, drawn by you with Python and Pillow. Build it with tools/make_theme.py, license CC-BY-SA-4.0, author "Me". Run tools/check_bin.py --gallery and fix every problem until there is none. Give me the path of the .bin.
```

For a face from a font:

```text
Read docs/making-bins.md in this repository. Make me a clock face called "Neon" with the font at ./MyFont.ttf in blue, with tools/make_face.py, license CC-BY-SA-4.0, author "Me". Check it with tools/check_bin.py --gallery and give me the path of the .bin.
```

## With a chat that has no terminal

Ask for the pictures only, and make the file in the [creator](/creator/index.html){target="_self"}:

```text
I am making an animation for a clock with six small screens side by side. Make four short animated GIFs, each exactly 1370 x 240 pixels at 15 frames per second, black background, a style of waves and surfboards in warm colors: 1) the clock starting (8 s), 2) looking for Wi-Fi (6 s, can loop), 3) connected (4 s), 4) the end of a countdown (6 s). The picture crosses the six screens, which are 135 pixels wide and 112 pixels apart, so keep the action inside them. Few colors and flat shapes.
```

Then open the creator, give it the four GIFs and download the theme. Upload it in **Files** on the page of your clock.

## Rules to tell the assistant

Every file carries a name, an author and a license, and the gallery takes `CC-BY-SA-4.0`. The assistant must draw its own pictures and not use characters, logos or brands that are not yours.
