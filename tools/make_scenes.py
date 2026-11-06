# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the scenes of the default theme and pack them as a theme file.

A scene is a sequence of canvases that cross the tubes. Like a GIF, it stores only the rectangles
that change between frames, so a small moving shape stays fast however far it travels.

Usage: python tools/make_scenes.py space.bin [--preview folder]

A theme file is a store with the four animations that a theme is made of, and its name and data. In
the page, Change theme takes it in place of the theme of the clock and keeps the faces.
"""

import argparse
import math
import random
import struct
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

from make_clips import CANVAS_WIDTH, HEIGHT, REGION_BYTES, TUBE_COUNT, TUBE_PITCH, WIDTH, Scene, build_library

TILE = 8
SS = 2  # supersampling of the drawn canvases
PATCH = struct.Struct("<BBBBBxH")  # tube, x, y, width, height, blob


def rle(indices):
    """Token below 128: repeat the next byte token + 1 times. Otherwise token - 127 literal bytes."""
    output = bytearray()
    literal = bytearray()

    def flush():
        if literal:
            output.append(127 + len(literal))
            output.extend(literal)
            literal.clear()

    position = 0
    length = len(indices)
    while position < length:
        value = indices[position]
        run = 1
        while position + run < length and run < 128 and indices[position + run] == value:
            run += 1
        if run >= 3:
            flush()
            output += bytes((run - 1, value))
            position += run
        else:
            literal.append(value)
            position += 1
            if len(literal) == 128:
                flush()
    flush()
    return bytes(output)


def dirty_rectangles(before, after):
    """Returns (x, y, width, height) rectangles that cover the changes between two tube images."""
    spans = {}
    for tile_row in range(0, HEIGHT, TILE):
        row_bottom = min(tile_row + TILE, HEIGHT)
        dirty = []
        for tile_column in range(0, WIDTH, TILE):
            width = min(TILE, WIDTH - tile_column)
            changed = any(
                before[y * WIDTH + tile_column : y * WIDTH + tile_column + width]
                != after[y * WIDTH + tile_column : y * WIDTH + tile_column + width]
                for y in range(tile_row, row_bottom)
            )
            dirty.append(changed)
        column = 0
        current = {}
        while column < len(dirty):
            if not dirty[column]:
                column += 1
                continue
            start = column
            while column < len(dirty) and dirty[column]:
                column += 1
            span = (start, column)
            top = spans.pop(span, tile_row)
            current[span] = top
        yield from (
            (start * TILE, top, min(end * TILE, WIDTH) - start * TILE, tile_row - top) for (start, end), top in spans.items()
        )
        spans = current
    yield from ((start * TILE, top, min(end * TILE, WIDTH) - start * TILE, HEIGHT - top) for (start, end), top in spans.items())


def merge_touching(rectangles):
    """Joins rectangles that touch, so one shape is always cut the same way."""
    merged = list(rectangles)
    changed = True
    while changed:
        changed = False
        for first in range(len(merged)):
            for second in range(first + 1, len(merged)):
                ax, ay, aw, ah = merged[first]
                bx, by, bw, bh = merged[second]
                if ax <= bx + bw and bx <= ax + aw and ay <= by + bh and by <= ay + ah:
                    left, top = min(ax, bx), min(ay, by)
                    merged[first] = (left, top, max(ax + aw, bx + bw) - left, max(ay + ah, by + bh) - top)
                    del merged[second]
                    changed = True
                    break
            if changed:
                break
    return merged


def split_foreground(before, after, x, y, width, height):
    """Splits a changed rectangle into the tight box of what is lit and the black around it.

    A shape on black then gives the same pixels wherever it is, so its frames can be shared.
    """
    rows = [after[row * WIDTH + x : row * WIDTH + x + width] for row in range(y, y + height)]
    lit = [index for index, row in enumerate(rows) if any(row)]
    if not lit:
        yield x, y, width, height
        return
    top, bottom = lit[0], lit[-1] + 1
    left = min(len(rows[index]) - len(rows[index].lstrip(b"\0")) for index in lit)
    right = max(len(rows[index].rstrip(b"\0")) for index in lit)

    def was_lit(box_x, box_y, box_width, box_height):
        return any(
            any(before[row * WIDTH + box_x : row * WIDTH + box_x + box_width])
            for row in range(box_y, box_y + box_height)
        )

    around = (
        (x, y, width, top),
        (x, y + bottom, width, height - bottom),
        (x, y + top, left, bottom - top),
        (x + right, y + top, width - right, bottom - top),
    )
    for box in around:
        if box[2] > 0 and box[3] > 0 and was_lit(*box):
            yield box
    yield x + left, y + top, right - left, bottom - top


def build_palette(scenes, colors=255):
    """Returns the palette shared by the scenes of a theme, so they can share their images."""
    samples = [canvas for canvases in scenes for canvas in canvases[:: max(1, len(canvases) // 8)]]
    sheet = Image.new("RGB", (CANVAS_WIDTH // 2, HEIGHT // 2 * len(samples)))
    for index, canvas in enumerate(samples):
        sheet.paste(canvas.resize((CANVAS_WIDTH // 2, HEIGHT // 2), Image.NEAREST), (0, index * (HEIGHT // 2)))
    found = sheet.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE).getpalette()
    entries = [(0, 0, 0)] + [tuple(found[i : i + 3]) for i in range(0, len(found), 3)]
    return entries + [(0, 0, 0)] * (256 - len(entries))


def region_light(image):
    """The color that a region of a picture asks of a light: the mean color of what is lit, as strong as
    the region is full of it, and dimmer when that is dark."""
    mean = image.resize((1, 1), Image.BOX).getpixel((0, 0))
    lit = image.convert("L").point(lambda value: 255 if value > 20 else 0).resize((1, 1), Image.BOX).getpixel((0, 0)) / 255
    if lit < 0.002:
        return (0.0, 0.0, 0.0)
    lit_color = [min(255.0, channel / lit) for channel in mean]
    peak = max(lit_color) or 1.0
    strength = min(1.0, math.sqrt(lit) * 2.2) * (0.4 + 0.6 * peak / 255)
    return tuple(channel * 255 / peak * strength for channel in lit_color)


def led_track(canvases, background=None, ambient=0.12, smoothing=0.55):
    """One color per LED for each frame, from the picture of its tube.

    With a background, which is what stays still in the scene, the lights follow what moves over it,
    and the background itself only adds a little. Without one they follow the whole picture. The LED of a
    tube has the number of the tube: the pictures run from the leftmost tube, which is tube 5.
    """
    track = bytearray()
    previous = [(0.0, 0.0, 0.0)] * TUBE_COUNT
    for canvas in canvases:
        tubes = []
        for tube in range(TUBE_COUNT):
            left = (TUBE_COUNT - 1 - tube) * TUBE_PITCH
            box = (left, 0, left + WIDTH, HEIGHT)
            crop = canvas.crop(box)
            if background is None:
                color = region_light(crop)
            else:
                moving = region_light(ImageChops.difference(crop, background.crop(box)))
                still = region_light(crop)
                color = tuple(min(255.0, m + ambient * s) for m, s in zip(moving, still))
            tubes.append(tuple(smoothing * now + (1 - smoothing) * before for now, before in zip(color, previous[tube])))
        previous = tubes
        for color in tubes:
            track += bytes(round(channel) for channel in color)
    return bytes(track)


def encode_scene(canvases, entries=None, background=None, lights=True):
    """Encodes canvases of CANVAS_WIDTH x HEIGHT, which start from black tubes."""
    entries = entries or build_palette([canvases])
    reference = Image.new("P", (1, 1))
    reference.putpalette([channel for entry in entries for channel in entry])

    palette = b"".join(
        struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in entries
    )
    # Every black entry becomes index 0, which is what the tubes show at the start.
    to_first_black = bytes(0 if entries[index] == (0, 0, 0) else index for index in range(256))
    shown = [bytes(WIDTH * HEIGHT) for _ in range(TUBE_COUNT)]
    blobs = {}
    records = []
    for canvas in canvases:
        indexed = canvas.quantize(palette=reference, dither=Image.NONE)
        patches = bytearray()
        count = 0
        for image_index in range(TUBE_COUNT):
            left = image_index * TUBE_PITCH
            current = indexed.crop((left, 0, left + WIDTH, HEIGHT)).tobytes().translate(to_first_black)
            if current == shown[image_index]:
                continue
            for rectangle in merge_touching(dirty_rectangles(shown[image_index], current)):
                for x, y, width, height in split_foreground(shown[image_index], current, *rectangle):
                    pixels = b"".join(current[row * WIDTH + x : row * WIDTH + x + width] for row in range(y, y + height))
                    blob = rle(pixels)
                    number = blobs.setdefault(blob, len(blobs))
                    # Images run left to right, and tube 5 is the leftmost one.
                    patches += PATCH.pack(TUBE_COUNT - 1 - image_index, x, y, width, height, number)
                    count += 1
            shown[image_index] = current
        records.append(struct.pack("<H", count) + bytes(patches))
    return Scene(palette, records, list(blobs), led_track(canvases, background) if lights else b"")


def new_canvas():
    return Image.new("RGB", (CANVAS_WIDTH * SS, HEIGHT * SS))


def finish(canvas):
    return canvas.resize((CANVAS_WIDTH, HEIGHT), Image.LANCZOS)


def draw_astronaut():
    """Returns the astronaut with a transparent background, on a square image."""
    size = 96
    scale = 8
    image = Image.new("RGBA", (size * scale, size * scale))
    draw = ImageDraw.Draw(image)

    def box(x0, y0, x1, y1, fill, radius=0):
        draw.rounded_rectangle([x * scale for x in (x0, y0, x1, y1)], radius * scale, fill=fill)

    def limb(x0, y0, x1, y1, width, fill):
        draw.line([x * scale for x in (x0, y0, x1, y1)], fill=fill, width=width * scale)
        for x, y in ((x0, y0), (x1, y1)):
            r = width / 2
            draw.ellipse([(x - r) * scale, (y - r) * scale, (x + r) * scale, (y + r) * scale], fill=fill)

    suit = (232, 230, 222, 255)
    shade = (176, 174, 166, 255)
    box(22, 36, 36, 70, (120, 118, 112, 255), 3)  # backpack
    limb(40, 66, 32, 86, 11, shade)
    limb(52, 66, 62, 84, 11, suit)
    limb(36, 44, 22, 60, 9, shade)
    limb(56, 44, 74, 34, 9, suit)
    box(32, 38, 60, 72, suit, 7)
    box(38, 50, 54, 56, (216, 90, 48, 255), 1)
    draw.ellipse([31 * scale, 12 * scale, 61 * scale, 42 * scale], fill=suit)
    draw.ellipse([38 * scale, 18 * scale, 58 * scale, 34 * scale], fill=(217, 154, 43, 255))
    draw.ellipse([42 * scale, 21 * scale, 47 * scale, 25 * scale], fill=(255, 240, 200, 255))
    return image.resize((size, size), Image.LANCZOS)


def bezier(p0, p1, p2, p3, steps=40):
    for index in range(steps + 1):
        t = index / steps
        u = 1 - t
        yield (
            u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
            u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1],
        )


def boot_scene(seconds=14, fps=15):
    """An astronaut leaves the ship on a tether that snaps, then slows and shrinks into the distance."""
    random.seed(7)
    stars = [(random.randrange(CANVAS_WIDTH), random.choice((random.randrange(0, 34), random.randrange(206, HEIGHT))),
              random.choice((90, 140, 200))) for _ in range(60)]
    astronaut = draw_astronaut()
    angle_step = 15
    rotated = {angle: astronaut.rotate(angle, Image.BICUBIC) for angle in range(0, 360, angle_step)}
    sprites = {}

    def sprite_for(angle, scale):
        # Sizes come in small steps, so frames can share them.
        step = max(1, round(scale * 32))
        key = (angle, step)
        if key not in sprites:
            size = max(3, round(astronaut.width * step / 32))
            sprites[key] = rotated[angle].resize((size, size), Image.LANCZOS)
        return sprites[key]

    ship_end = (74, 120)
    snap = 0.43  # the tether snaps at this point of the scene, on the centre right tube
    snap_x = 800
    final_x = 5 * TUBE_PITCH + WIDTH // 2  # the middle of the rightmost tube
    final_scale = 0.09
    recoil = 1.5  # seconds for the loose end to fall back
    tether_rate = 3  # frames per tether redraw, which keeps the changes small
    frames = []
    count = seconds * fps
    for frame in range(count):
        progress = frame / (count - 1)
        loose = progress > snap
        away = (progress - snap) / (1 - snap) if loose else 0.0
        canvas = new_canvas()
        draw = ImageDraw.Draw(canvas)
        for x, y, level in stars:
            draw.rectangle([x * SS, y * SS, x * SS + 2, y * SS + 2], fill=(level, level, level))
        draw.polygon([(p[0] * SS, p[1] * SS) for p in ((0, 62), (62, 76), (74, 120), (62, 164), (0, 178))], fill=(62, 60, 56))
        draw.rectangle([0, 104 * SS, 52 * SS, 136 * SS], fill=(95, 94, 90))
        draw.ellipse([24 * SS, 80 * SS, 36 * SS, 92 * SS], fill=(127, 216, 255))

        # After the snap it slows down, and the distance makes it shrink towards a dot.
        if loose:
            x = snap_x + (final_x - snap_x) * (1 - (1 - away) ** 1.6)
            scale = final_scale + (1 - final_scale) * (1 - away) ** 2.2
        else:
            x = 90 + (snap_x - 90) * progress / snap
            scale = 1.0
        drift_y = 120 + math.sin(progress * 9) * 30
        y = 120 + (drift_y - 120) * scale  # the far distance also flattens its bobbing
        turn = progress * 150 if not loose else snap * 150 + away * 300
        angle = round((math.sin(progress * 5) * 20 + turn) / angle_step) * angle_step % 360
        hand = (x - 26 * scale, y + 6 * scale)

        wave = (frame - frame % tether_rate) / fps
        tether = (224, 176, 80)
        if not loose:
            mid = (ship_end[0] + hand[0]) / 2
            points = bezier(ship_end, (mid * 0.7, 150 + math.sin(wave * 1.4) * 44), (mid * 1.25, y - math.sin(wave) * 44), hand)
            draw.line([(px * SS, py * SS) for px, py in points], fill=tether, width=2 * SS)
        else:
            # The ship's end falls back from where the tether snapped.
            since = (progress - snap) * seconds
            settle = min(1.0, since / recoil)
            settle = 1 - (1 - settle) ** 3
            end = (snap_x - 26 + (480 - snap_x + 26) * settle, 120 + 10 * settle)
            points = bezier(ship_end, (240, 160 + math.sin(wave * 2) * 18), (360, 96 - math.sin(wave * 1.6) * 20), end)
            draw.line([(px * SS, py * SS) for px, py in points], fill=tether, width=2 * SS)
            # A piece stays on the astronaut and shrinks with it.
            stub = 70 * scale
            points = bezier(hand, (hand[0] - stub * 0.4, hand[1] + 10 * scale + math.sin(wave * 3) * 8 * scale),
                            (hand[0] - stub * 0.7, hand[1] - 8 * scale), (hand[0] - stub, hand[1] + math.sin(wave * 2) * 10 * scale))
            draw.line([(px * SS, py * SS) for px, py in points], fill=tether, width=max(1, round(2 * SS * scale)))
        canvas = finish(canvas)
        sprite = sprite_for(angle, scale)
        canvas.paste(sprite, (round(x - sprite.width / 2), round(y - sprite.height / 2)), sprite)
        frames.append(canvas)
    return fps, frames


MOON_RADIUS = 3200
MOON_CENTRE = (CANVAS_WIDTH // 2, 175 + MOON_RADIUS)
ORBIT_ALTITUDE = 95
LANDING_X = 3 * TUBE_PITCH + WIDTH // 2  # the centre right tube
LM_FEET = 24  # from the centre of the sprite to the footpads


def moon_surface(x):
    return MOON_CENTRE[1] - math.sqrt(MOON_RADIUS**2 - (x - MOON_CENTRE[0]) ** 2)


def draw_moon():
    """The static background: stars, the Earth and the curved horizon of the Moon."""
    random.seed(11)
    canvas = new_canvas()
    draw = ImageDraw.Draw(canvas)
    for _ in range(60):
        x, y, level = random.randrange(CANVAS_WIDTH), random.randrange(0, 45), random.choice((90, 140, 200))
        draw.rectangle([x * SS, y * SS, x * SS + 2, y * SS + 2], fill=(level, level, level))

    earth = (TUBE_PITCH + 95, 32)
    r = 14
    draw.ellipse([(earth[0] - r) * SS, (earth[1] - r) * SS, (earth[0] + r) * SS, (earth[1] + r) * SS], fill=(55, 138, 221))
    draw.ellipse([(earth[0] - 7) * SS, (earth[1] - 8) * SS, (earth[0] + 4) * SS, (earth[1] - 1) * SS], fill=(235, 240, 240))
    draw.ellipse([(earth[0] - 3) * SS, (earth[1] + 2) * SS, (earth[0] + 7) * SS, (earth[1] + 8) * SS], fill=(93, 202, 165))
    draw.pieslice([(earth[0] - r) * SS, (earth[1] - r) * SS, (earth[0] + r) * SS, (earth[1] + r) * SS], 300, 120, fill=(0, 0, 0))

    cx, cy = MOON_CENTRE
    draw.ellipse([(cx - MOON_RADIUS) * SS, (cy - MOON_RADIUS) * SS, (cx + MOON_RADIUS) * SS, (cy + MOON_RADIUS) * SS], fill=(168, 166, 158))
    inner = MOON_RADIUS - 2
    draw.ellipse([(cx - inner) * SS, (cy - inner) * SS, (cx + inner) * SS, (cy + inner) * SS], fill=(112, 110, 104))
    for _ in range(70):
        x = random.randrange(CANVAS_WIDTH)
        y = moon_surface(x) + random.uniform(8, 62)
        width = random.uniform(5, 20)
        height = width * random.uniform(0.25, 0.4)
        draw.ellipse([(x - width) * SS, (y - height) * SS, (x + width) * SS, (y + height) * SS], fill=random.choice(((88, 86, 81), (96, 94, 88))))
        draw.arc([(x - width) * SS, (y - height) * SS, (x + width) * SS, (y + height) * SS], 20, 160, fill=(150, 148, 140), width=SS)
    return finish(canvas)


def draw_lunar_module(flame=0):
    """The Apollo lander, centred on a square image, with an engine flame of size 0, 1 or 2."""
    size = 96
    scale = 8
    image = Image.new("RGBA", (size * scale, size * scale))
    draw = ImageDraw.Draw(image)

    def polygon(points, fill):
        draw.polygon([(x * scale, y * scale) for x, y in points], fill=fill)

    def line(points, fill, width):
        draw.line([(x * scale, y * scale) for x, y in points], fill=fill, width=width * scale)

    if flame:
        length = 14 if flame == 1 else 20
        polygon(((44, 60), (48, 62 + length), (52, 60)), (255, 154, 42, 255))
        polygon(((46, 60), (48, 58 + length * 0.7), (50, 60)), (255, 226, 138, 255))
    gold = (217, 164, 48, 255)
    dark_gold = (166, 118, 28, 255)
    strut = (190, 188, 180, 255)
    for side in (-1, 1):
        line(((48 + side * 16, 48), (48 + side * 28, 70)), strut, 2)
        line(((48 + side * 8, 56), (48 + side * 28, 70)), strut, 1)
        draw.ellipse([(48 + side * 28 - 5) * scale, 69 * scale, (48 + side * 28 + 5) * scale, 73 * scale], fill=strut)
    polygon(((42, 54), (54, 54), (56, 62), (40, 62)), (120, 118, 112, 255))  # engine bell
    polygon(((30, 40), (66, 40), (70, 45), (70, 52), (66, 57), (30, 57), (26, 52), (26, 45)), gold)
    polygon(((30, 50), (66, 50), (70, 52), (66, 57), (30, 57), (26, 52)), dark_gold)
    polygon(((36, 20), (60, 20), (66, 28), (66, 40), (30, 40), (30, 28)), (214, 212, 204, 255))
    polygon(((30, 33), (66, 33), (66, 40), (30, 40)), (160, 158, 150, 255))
    polygon(((40, 24), (47, 24), (47, 31), (38, 31)), (30, 34, 44, 255))
    polygon(((49, 24), (56, 24), (58, 31), (49, 31)), (30, 34, 44, 255))
    line(((60, 20), (66, 10)), strut, 1)
    draw.ellipse([63 * scale, 6 * scale, 70 * scale, 11 * scale], fill=strut)
    return image.resize((size, size), Image.LANCZOS)


def lunar_module_painter():
    """Returns a function that pastes the lander, sharing the rotated images between frames."""
    angle_step = 5
    sources = {flame: draw_lunar_module(flame) for flame in (0, 1, 2)}
    rotated = {}

    def paint(canvas, x, y, angle, flame=0):
        angle = round(angle / angle_step) * angle_step % 360
        key = (angle, flame)
        if key not in rotated:
            rotated[key] = sources[flame].rotate(angle, Image.BICUBIC)
        sprite = rotated[key]
        canvas.paste(sprite, (round(x - sprite.width / 2), round(y - sprite.height / 2)), sprite)

    return paint


def orbit_pose(x):
    """Where the lander flies in orbit: engine first, following the curve of the Moon."""
    slope = (x - MOON_CENTRE[0]) / (MOON_CENTRE[1] - moon_surface(x))
    return moon_surface(x) - ORBIT_ALTITUDE, 72 - math.degrees(math.atan(slope))


def orbit_scene(seconds=8, fps=15):
    """The lander crosses every tube above the Moon, while the clock connects. It loops seamlessly."""
    moon = draw_moon()
    paint = lunar_module_painter()
    start, end = -60, CANVAS_WIDTH + 60
    frames = []
    count = seconds * fps
    for frame in range(count):
        x = start + (end - start) * frame / count
        y, angle = orbit_pose(x)
        canvas = moon.copy()
        paint(canvas, x, y, angle)
        frames.append(canvas)
    return fps, frames


def landing_scene(fps=15):
    """Starting where the orbit does, the lander brakes, turns upright and lands on the centre right tube."""
    moon = draw_moon()
    paint = lunar_module_painter()
    random.seed(5)
    braking, descending, settling = 5.5, 3.0, 2.5
    site = moon_surface(LANDING_X) - LM_FEET
    hover = moon_surface(LANDING_X) - ORBIT_ALTITUDE
    puffs = [(random.choice((-1, 1)) * random.uniform(8, 66), random.uniform(0, 12), random.uniform(3, 8)) for _ in range(22)]
    frames = []
    count = round((braking + descending + settling) * fps)
    for frame in range(count):
        t = frame / fps
        canvas = moon.copy()
        flame = 0
        if t < braking:
            s = t / braking
            x = -60 + (LANDING_X + 60) * (1 - (1 - s) ** 2.2)
            y, tilt = orbit_pose(x)
            turn = min(1.0, max(0.0, (s - 0.5) / 0.45))
            y += (hover - y) * turn
            tilt *= 1 - turn * turn * (3 - 2 * turn)
            if s > 0.35:
                flame = 1 + frame % 2
        else:
            x, tilt = LANDING_X, 0
            s = min(1.0, (t - braking) / descending)
            y = hover + (site - hover) * (1 - (1 - s) ** 1.7)
            if s < 1:
                flame = 1 + frame % 2

        # Dust rises in the last part of the descent and settles after touchdown.
        since = t - (braking + descending * 0.55)
        if since > 0:
            rise = min(1.0, since / 1.2)
            fall = max(0.0, 1 - max(0.0, t - braking - descending) / settling)
            draw = ImageDraw.Draw(canvas)
            ground = moon_surface(LANDING_X)
            level = round(112 + 58 * fall)
            for offset, lift, size in puffs:
                radius = size * rise * (0.4 + 0.6 * fall)
                if radius < 0.8:
                    continue
                px = LANDING_X + offset * rise
                py = ground - lift * rise * fall + 3
                draw.ellipse([px - radius, py - radius, px + radius, py + radius], fill=(level, level - 2, level - 8))
        paint(canvas, x, y, tilt, flame)
        frames.append(canvas)
    return fps, frames


def launch_scene(fps=15):
    """The tower leans back on the centre left tube as the rocket lifts off on the centre right one."""
    left = 2 * TUBE_PITCH
    right = 3 * TUBE_PITCH
    pad = 210
    frames = []
    random.seed(3)
    puffs = [(random.uniform(-1, 1), random.uniform(0, 1), random.uniform(0.7, 1.3)) for _ in range(26)]
    count = 6 * fps
    for frame in range(count):
        t = frame / fps
        canvas = new_canvas()
        draw = ImageDraw.Draw(canvas)
        scaled = lambda points: [(px * SS, py * SS) for px, py in points]
        draw.rectangle([left * SS, pad * SS, (left + WIDTH) * SS, HEIGHT * SS], fill=(44, 44, 42))
        draw.rectangle([right * SS, pad * SS, (right + WIDTH) * SS, HEIGHT * SS], fill=(44, 44, 42))

        lean = min(1.0, max(0.0, t / 1.5)) * 0.34
        base = (left + 40, pad)
        tower = lambda dx, dy: (base[0] + dx * math.cos(lean) + dy * math.sin(lean), base[1] + dy * math.cos(lean) - dx * math.sin(lean))
        for dx in (-11, 11):
            draw.line(scaled((tower(dx, 0), tower(dx, -190))), fill=(163, 58, 44), width=4 * SS)
        for dy in range(0, 190, 22):
            draw.line(scaled((tower(-11, -dy), tower(11, -dy - 22))), fill=(163, 58, 44), width=2 * SS)
            draw.line(scaled((tower(11, -dy), tower(-11, -dy - 22))), fill=(163, 58, 44), width=2 * SS)
        reach = (1 - min(1.0, t / 1.5)) * 86 + 4
        for dy in (-150, -95, -45):
            draw.line(scaled((tower(11, dy), tower(11 + reach, dy))), fill=(211, 209, 199), width=5 * SS)

        rise = 0 if t < 1.6 else (t - 1.6) ** 2 * 34
        cx = right + WIDTH // 2
        bottom = pad - rise
        if t > 0.8:
            flame = 38 + 9 * math.sin(frame * 2.1) + min(30, rise * 0.15)
            draw.polygon(scaled(((cx - 14, bottom - 6), (cx, bottom + flame), (cx + 14, bottom - 6))), fill=(255, 154, 42))
            draw.polygon(scaled(((cx - 7, bottom - 6), (cx, bottom + flame * 0.7), (cx + 7, bottom - 6))), fill=(255, 226, 138))
        draw.polygon(scaled(((cx - 17, bottom - 8), (cx - 17, bottom - 112), (cx, bottom - 165), (cx + 17, bottom - 112), (cx + 17, bottom - 8))), fill=(233, 231, 223))
        draw.polygon(scaled(((cx - 13, bottom - 122), (cx, bottom - 165), (cx + 13, bottom - 122))), fill=(216, 90, 48))
        draw.polygon(scaled(((cx - 17, bottom - 8), (cx - 32, bottom + 6), (cx - 17, bottom - 42))), fill=(216, 90, 48))
        draw.polygon(scaled(((cx + 17, bottom - 8), (cx + 32, bottom + 6), (cx + 17, bottom - 42))), fill=(216, 90, 48))
        draw.ellipse(scaled(((cx - 8, bottom - 94), (cx + 8, bottom - 78))), fill=(55, 138, 221))
        if t > 0.8:
            smoke = min(1.0, (t - 0.8) / 1.6)
            for spread, lift, size in puffs:
                px = cx + spread * smoke * (WIDTH + 112 + 60)
                py = pad - lift * 26 * smoke
                r = (10 + smoke * 16) * size
                level = int(170 - lift * 40)
                draw.ellipse(scaled(((px - r, py - r), (px + r, py + r))), fill=(level, level - 4, level - 10))
        frames.append(finish(canvas))
    return fps, frames


def draw_launch_ground():
    """The pads of the launch scene, which are all that stays still in it."""
    canvas = Image.new("RGB", (CANVAS_WIDTH, HEIGHT))
    draw = ImageDraw.Draw(canvas)
    for tube in (2, 3):
        draw.rectangle([tube * TUBE_PITCH, 210, tube * TUBE_PITCH + WIDTH, HEIGHT], fill=(44, 44, 42))
    return canvas


THEME = ("Space", {
    "author": "Dekatron",
    "license": "CC-BY-SA-4.0",
    "description": "An astronaut on a tether, a lander in lunar orbit, and a rocket launch",
})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview", type=Path, help="write an animated GIF per scene to this folder")
    args = parser.parse_args()

    makers = [("boot", boot_scene), ("connecting", orbit_scene), ("online", landing_scene), ("countdown_end", launch_scene)]
    made = [(name, *maker()) for name, maker in makers]
    entries = build_palette([canvases for _, _, canvases in made])
    # What stays still in a scene, so that the lights follow what moves over it.
    backgrounds = {"connecting": draw_moon(), "online": draw_moon(), "countdown_end": draw_launch_ground(),
                   "boot": made[0][2][0]}  # the ship and the stars are the first picture, before anything moves
    clips = []
    for name, fps, canvases in made:
        scene = encode_scene(canvases, entries, backgrounds.get(name))
        size = len(scene.palette) + sum(map(len, scene.records)) + sum(map(len, scene.blobs))
        patches = sum(struct.unpack_from("<H", record)[0] for record in scene.records)
        pixels = sum(1 for _ in scene.blobs)
        print(f"{name}: {len(canvases)} frames at {fps} fps, {size} bytes, {patches} patches, {pixels} unique blobs, lights {len(scene.leds)} bytes")
        clips.append((name, fps, scene))
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            canvases[0].save(args.preview / f"{name}.gif", save_all=True, append_images=canvases[1:],
                             duration=1000 // fps, loop=0)
    try:
        library = build_library(clips, theme=THEME)
    except ValueError as error:
        sys.exit(str(error))
    args.output.write_bytes(library)
    print(f"{args.output}: {len(library)} of {REGION_BYTES} bytes")


if __name__ == "__main__":
    main()
