#include "text.h"

#include <cstring>

#include "text_font.h"

namespace esphome {
namespace elekstube_ips {

static constexpr uint16_t WIDTH = 135;
static constexpr uint16_t SCALE = 2;  // screen pixels per font pixel
static constexpr uint16_t GAP = 4;    // font pixels between glyphs
static constexpr size_t GLYPH_COUNT = sizeof(FONT_GLYPHS) / sizeof(FONT_GLYPHS[0]);
static constexpr size_t ICON_NAME_MAX = 16;

static uint8_t glyph_index(uint16_t codepoint) {
  size_t low = 0;
  size_t high = GLYPH_COUNT;
  while (low < high) {
    const size_t middle = (low + high) / 2;
    if (FONT_GLYPHS[middle].codepoint < codepoint) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < GLYPH_COUNT && FONT_GLYPHS[low].codepoint == codepoint) {
    return static_cast<uint8_t>(low);
  }
  return codepoint == '?' ? 0 : glyph_index('?');
}

// Returns the codepoint at `text` and moves it past that character. Malformed input yields '?'.
static uint32_t next_codepoint(const char *&text) {
  const auto lead = static_cast<uint8_t>(*text++);
  uint8_t extra;
  uint32_t codepoint;
  if (lead < 0x80) {
    return lead;
  } else if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    codepoint = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    codepoint = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    codepoint = lead & 0x07;
  } else {
    return '?';
  }
  for (uint8_t index = 0; index < extra; index++) {
    const auto next = static_cast<uint8_t>(*text);
    if ((next & 0xC0) != 0x80) {
      return '?';
    }
    codepoint = (codepoint << 6) | (next & 0x3F);
    text++;
  }
  return codepoint;
}

static bool parse_icon(const char *&text, uint16_t *codepoint) {
  const char *end = strchr(text + 1, '}');
  if (end == nullptr) {
    return false;
  }
  const size_t length = end - text - 1;
  if (length == 0 || length >= ICON_NAME_MAX) {
    return false;
  }
  for (const FontIcon &icon : FONT_ICONS) {
    if (strlen(icon.name) == length && strncmp(icon.name, text + 1, length) == 0) {
      *codepoint = icon.codepoint;
      text = end + 1;
      return true;
    }
  }
  return false;
}

size_t text_parse(const char *utf8, uint8_t *glyphs) {
  size_t count = 0;
  const char *cursor = utf8;
  while (*cursor != '\0' && count < TEXT_MAX_GLYPHS) {
    uint16_t icon;
    if (*cursor == '{' && parse_icon(cursor, &icon)) {
      glyphs[count++] = glyph_index(icon);
      continue;
    }
    const uint32_t codepoint = next_codepoint(cursor);
    glyphs[count++] = glyph_index(codepoint > 0xFFFF ? '?' : static_cast<uint16_t>(codepoint));
  }
  return count;
}

uint16_t text_glyph_width(uint8_t glyph) { return FONT_GLYPHS[glyph].width * SCALE; }

uint32_t text_width(const uint8_t *glyphs, size_t count) {
  uint32_t width = 0;
  for (size_t index = 0; index < count; index++) {
    width += (FONT_GLYPHS[glyphs[index]].width + GAP) * SCALE;
  }
  return width;
}

bool text_columns(const uint8_t *glyphs, size_t count, int32_t text_left, TextColumn *columns) {
  memset(columns, 0, WIDTH * sizeof(TextColumn));
  bool touched = false;
  int32_t left = text_left;
  for (size_t index = 0; index < count && left < WIDTH; index++) {
    const FontGlyph &glyph = FONT_GLYPHS[glyphs[index]];
    const int32_t right = left + glyph.width * SCALE;
    if (right > 0) {
      const uint8_t *bits = FONT_BITS + glyphs[index] * FONT_GLYPH_BYTES;
      for (int32_t x = left < 0 ? 0 : left; x < right && x < WIDTH; x++) {
        const uint16_t font_x = (x - left) / SCALE;
        columns[x].bits = bits + font_x / 8;
        columns[x].mask = 1 << (font_x % 8);
      }
      touched = true;
    }
    left = right + GAP * SCALE;
  }
  return touched;
}

void text_line(const TextColumn *columns, uint16_t row, uint16_t foreground, uint16_t *line) {
  const size_t offset = (row / SCALE) * FONT_ROW_BYTES;
  for (uint16_t x = 0; x < WIDTH; x++) {
    const TextColumn &column = columns[x];
    line[x] = (column.bits != nullptr && (column.bits[offset] & column.mask) != 0) ? foreground : 0;
  }
}

}  // namespace elekstube_ips
}  // namespace esphome
