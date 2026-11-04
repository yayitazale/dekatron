"""Generate the 1 bit font and icons embedded in the firmware for the text action."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUTPUT = Path(__file__).parent.parent / "components" / "elekstube_ips" / "text_font.h"
GLYPH_WIDTH, GLYPH_HEIGHT = 48, 72
ROW_BYTES = GLYPH_WIDTH // 8
FONT_SIZE = 54
BASELINE = 58
EXTRA_CHARACTERS = "ÁÉÍÓÚÜÑáéíóúüñ¿¡°€"
ICON_BASE = 0xE000
# The default Pillow font has no accents, so these are built from a base letter and a drawn mark.
COMPOSED = {
    "Á": ("A", "acute"), "É": ("E", "acute"), "Í": ("I", "acute"), "Ó": ("O", "acute"), "Ú": ("U", "acute"),
    "Ü": ("U", "diaeresis"), "Ñ": ("N", "tilde"),
    "á": ("a", "acute"), "é": ("e", "acute"), "í": ("i", "dotless_acute"), "ó": ("o", "acute"), "ú": ("u", "acute"),
    "ü": ("u", "diaeresis"), "ñ": ("n", "tilde"),
    "¿": ("?", "flip"), "¡": ("!", "flip"), "€": ("C", "bars"),
}


def draw_warning(draw):
    draw.polygon([(24, 8), (46, 62), (2, 62)], fill=255)
    draw.rectangle((21, 26, 27, 46), fill=0)
    draw.rectangle((21, 50, 27, 56), fill=0)


def draw_bell(draw):
    draw.pieslice((8, 8, 40, 48), 180, 360, fill=255)
    draw.rectangle((8, 28, 40, 50), fill=255)
    draw.polygon([(8, 44), (40, 44), (46, 56), (2, 56)], fill=255)
    draw.ellipse((18, 56, 30, 66), fill=255)


def draw_drop(draw):
    draw.polygon([(24, 6), (42, 40), (6, 40)], fill=255)
    draw.ellipse((6, 26, 42, 62), fill=255)


def draw_sun(draw):
    draw.ellipse((13, 23, 35, 45), fill=255)
    for start, end in (((24, 6), (24, 16)), ((24, 52), (24, 62)), ((2, 34), (10, 34)), ((38, 34), (46, 34)),
                       ((8, 18), (14, 24)), ((34, 44), (40, 50)), ((40, 18), (34, 24)), ((14, 44), (8, 50))):
        draw.line((start, end), fill=255, width=5)


def draw_heart(draw):
    draw.ellipse((3, 14, 26, 38), fill=255)
    draw.ellipse((22, 14, 45, 38), fill=255)
    draw.polygon([(4, 32), (44, 32), (24, 60)], fill=255)


def draw_lock(draw):
    draw.arc((11, 8, 37, 44), 180, 360, fill=255, width=6)
    draw.rectangle((11, 26, 16, 34), fill=255)
    draw.rectangle((32, 26, 37, 34), fill=255)
    draw.rectangle((5, 32, 43, 64), fill=255)
    draw.ellipse((20, 40, 28, 48), fill=0)
    draw.rectangle((22, 44, 26, 56), fill=0)


ICONS = (
    ("warning", draw_warning),
    ("bell", draw_bell),
    ("drop", draw_drop),
    ("sun", draw_sun),
    ("heart", draw_heart),
    ("lock", draw_lock),
)


def pack_bits(image):
    data = bytearray()
    for y in range(GLYPH_HEIGHT):
        for byte_x in range(ROW_BYTES):
            value = 0
            for bit in range(8):
                if image.getpixel((byte_x * 8 + bit, y)) >= 128:
                    value |= 1 << bit
            data.append(value)
    return bytes(data)


def draw_mark(draw, mark, box):
    centre = (box[0] + box[2]) // 2
    top = box[1]
    if mark == "acute":
        draw.polygon([(centre - 3, top - 4), (centre + 4, top - 14), (centre + 9, top - 12), (centre + 2, top - 3)], fill=255)
    elif mark == "tilde":
        draw.line([(centre - 11, top - 5), (centre - 5, top - 10), (centre + 4, top - 6), (centre + 10, top - 11)],
                  fill=255, width=4)
    elif mark == "diaeresis":
        draw.rectangle((centre - 10, top - 11, centre - 4, top - 5), fill=255)
        draw.rectangle((centre + 4, top - 11, centre + 10, top - 5), fill=255)
    elif mark == "bars":
        draw.rectangle((box[0] - 2, 29, box[0] + 18, 33), fill=255)
        draw.rectangle((box[0] - 2, 37, box[0] + 18, 41), fill=255)


def render_character(character, font):
    base, mark = COMPOSED.get(character, (character, None))
    image = Image.new("L", (GLYPH_WIDTH * 2, GLYPH_HEIGHT))
    draw = ImageDraw.Draw(image)
    draw.text((4, BASELINE), base, fill=255, font=font, anchor="ls", stroke_width=1, stroke_fill=255)
    box = image.getbbox()
    if box is None:
        return Image.new("L", (GLYPH_WIDTH, GLYPH_HEIGHT)), GLYPH_WIDTH // 3
    if mark == "flip":
        flipped = image.crop(box).rotate(180)
        image = Image.new("L", image.size)
        image.paste(flipped, (box[0], BASELINE + 12 - (box[3] - box[1])))
    elif mark == "dotless_acute":
        draw.rectangle((0, 0, box[2] + 2, box[1] + 10), fill=0)
        draw_mark(draw, "acute", image.getbbox())
    elif mark:
        draw_mark(draw, mark, box)
    box = image.getbbox()
    glyph_width = min(box[2] - box[0], GLYPH_WIDTH)
    return image.crop((box[0], 0, box[0] + GLYPH_WIDTH, GLYPH_HEIGHT)), glyph_width


def render_icon(draw_icon):
    image = Image.new("L", (GLYPH_WIDTH, GLYPH_HEIGHT))
    draw_icon(ImageDraw.Draw(image))
    return image, GLYPH_WIDTH


def build_glyphs():
    font = ImageFont.load_default(FONT_SIZE)
    characters = sorted({chr(code) for code in range(32, 127)} | set(EXTRA_CHARACTERS))
    glyphs = [(ord(character), *render_character(character, font)) for character in characters]
    for index, (_, draw_icon) in enumerate(ICONS):
        glyphs.append((ICON_BASE + index, *render_icon(draw_icon)))
    return glyphs


def preview(glyphs, path):
    columns = 16
    rows = len(glyphs) // columns + 1
    sheet = Image.new("L", (columns * (GLYPH_WIDTH + 4), rows * (GLYPH_HEIGHT + 4)))
    for index, (_, image, _) in enumerate(glyphs):
        sheet.paste(image.point(lambda value: 255 if value >= 128 else 0),
                    ((index % columns) * (GLYPH_WIDTH + 4), (index // columns) * (GLYPH_HEIGHT + 4)))
    sheet.save(path)


def main(preview_path=None):
    glyphs = build_glyphs()
    if preview_path:
        preview(glyphs, preview_path)

    bytes_per_glyph = ROW_BYTES * GLYPH_HEIGHT
    bitmap = b"".join(pack_bits(image) for _, image, _ in glyphs)
    rows = [", ".join(f"0x{b:02X}" for b in bitmap[i : i + 24]) for i in range(0, len(bitmap), 24)]
    body = ",\n    ".join(rows)
    table = "\n".join(f"    {{0x{code:04X}, {width}}}," for code, _, width in glyphs)
    icons = "\n".join(f'    {{"{name}", 0x{ICON_BASE + index:04X}}},' for index, (name, _) in enumerate(ICONS))
    print(f"{len(glyphs)} glyphs, {len(bitmap)} bytes")
    OUTPUT.write_text(
        "// Generated by tools/make_font.py\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n"
        "namespace esphome {\nnamespace elekstube_ips {\n\n"
        f"static constexpr uint8_t FONT_ROWS = {GLYPH_HEIGHT};\n"
        f"static constexpr uint8_t FONT_ROW_BYTES = {ROW_BYTES};\n"
        f"static constexpr size_t FONT_GLYPH_BYTES = {bytes_per_glyph};\n\n"
        "struct FontGlyph {\n  uint16_t codepoint;\n  uint8_t width;\n};\n\n"
        "struct FontIcon {\n  const char *name;\n  uint16_t codepoint;\n};\n\n"
        f"// Sorted by codepoint.\nstatic const FontGlyph FONT_GLYPHS[] = {{\n{table}\n}};\n\n"
        f"static const FontIcon FONT_ICONS[] = {{\n{icons}\n}};\n\n"
        f"static const uint8_t FONT_BITS[{len(bitmap)}] = {{\n    {body},\n}};\n\n"
        "}  // namespace elekstube_ips\n}  // namespace esphome\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    import sys

    main(sys.argv[1] if len(sys.argv) > 1 else None)
