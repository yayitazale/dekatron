#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace elekstube_ips {

static constexpr size_t TEXT_MAX_GLYPHS = 64;
static constexpr uint16_t TEXT_FIRST_ROW = 48;
static constexpr uint16_t TEXT_ROWS = 144;

// One screen column: the font column it shows, or nullptr when it is empty.
struct TextColumn {
  const uint8_t *bits;
  uint8_t mask;
};

// Decodes UTF-8 and {icon} names into glyph indices. Unknown characters become '?'.
// Returns the number of glyphs, truncated to TEXT_MAX_GLYPHS.
size_t text_parse(const char *utf8, uint8_t *glyphs);

// Width in screen pixels when the glyphs are laid out one after another.
uint32_t text_width(const uint8_t *glyphs, size_t count);

// Resolves the columns of a 135 pixel tube, with the text starting at `text_left` relative to
// the left edge of the tube. Returns false when no column of the tube touches the text.
bool text_columns(const uint8_t *glyphs, size_t count, int32_t text_left, TextColumn *columns);

// Width in screen pixels of one glyph, to centre it on a tube.
uint16_t text_glyph_width(uint8_t glyph);

// Fills one line of 135 pixels. `row` goes from 0 to TEXT_ROWS - 1.
void text_line(const TextColumn *columns, uint16_t row, uint16_t foreground, uint16_t *line);

}  // namespace elekstube_ips
}  // namespace esphome
