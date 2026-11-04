#include "effects.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace esphome {
namespace elekstube_ips {

static constexpr uint16_t WIDTH = 135;
static constexpr uint16_t HEIGHT = 240;

static inline uint32_t mix_hash(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  return value ^ (value >> 16);
}

static inline uint16_t wire_color(uint16_t r, uint16_t g, uint16_t b) {
  const uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  return static_cast<uint16_t>((color >> 8) | (color << 8));
}

// level is 0 to 256.
static inline uint16_t scaled(const Rgb &color, uint16_t level) {
  return wire_color((color.r * level) >> 8, (color.g * level) >> 8, (color.b * level) >> 8);
}

static void fill_rows_from_first(uint16_t *strip, uint16_t rows) {
  for (uint16_t row = 1; row < rows; row++) {
    memcpy(strip + row * WIDTH, strip, WIDTH * sizeof(uint16_t));
  }
}

static void render_matrix(const EffectFrame &frame, uint16_t *strip, uint16_t first_row, uint16_t rows) {
  static constexpr uint16_t CELL = 15;
  static constexpr uint16_t COLUMNS = WIDTH / CELL;
  static constexpr uint16_t CELL_ROWS = HEIGHT / CELL;
  static constexpr uint16_t TRAIL = 9;
  static constexpr uint16_t CYCLE = CELL_ROWS + TRAIL;
  static constexpr uint32_t GLYPH_MS = 350;

  const Rgb head = {255, 255, 255};
  const uint32_t glyph_slot = frame.elapsed_ms / GLYPH_MS;
  uint16_t *pixel = strip;
  for (uint16_t y = first_row; y < first_row + rows; y++) {
    const uint16_t cell_row = y / CELL;
    const uint16_t line = y % CELL;
    for (uint16_t column = 0; column < COLUMNS; column++) {
      const uint32_t seed = mix_hash(frame.tube * 97u + column * 31u + 1u);
      const uint32_t speed = 6 + seed % 10;  // cells per second
      const uint32_t position = ((seed >> 8) % CYCLE + frame.elapsed_ms * speed / 1000) % CYCLE;
      const uint16_t behind = (position + CYCLE - cell_row) % CYCLE;

      uint16_t on_color = 0;
      uint8_t bits = 0;
      if (behind < TRAIL) {
        const uint16_t level = (TRAIL - behind) * 256 / TRAIL;
        on_color = behind == 0 ? scaled(head, frame.dim) : scaled(frame.color, (level * frame.dim) >> 8);
        // A 5x7 glyph at double size: 14 lines tall, 10 pixels wide.
        if (line < 14) {
          const uint32_t glyph = mix_hash(column * 257u + cell_row * 65537u + glyph_slot + frame.tube) & 0x3F;
          bits = mix_hash(glyph * 7u + line / 2) & 0x1F;
        }
      }
      for (uint16_t x = 0; x < CELL; x++) {
        const bool lit = x < 10 && ((bits >> (x / 2)) & 1);
        *pixel++ = lit ? on_color : 0;
      }
    }
  }
}

static void render_glitch(const EffectFrame &frame, uint16_t *strip, uint16_t first_row, uint16_t rows) {
  static constexpr uint16_t BAND = 8;
  static constexpr uint32_t SLOT_MS = 90;
  const uint32_t slot = frame.elapsed_ms / SLOT_MS;
  const Rgb inverse = {static_cast<uint8_t>(255 - frame.color.r), static_cast<uint8_t>(255 - frame.color.g),
                       static_cast<uint8_t>(255 - frame.color.b)};
  uint16_t *pixel = strip;
  for (uint16_t y = first_row; y < first_row + rows; y++) {
    const uint32_t hash = mix_hash((y / BAND) * 131u + slot * 8191u + frame.tube);
    const uint16_t bright = scaled((hash & 8) != 0 ? frame.color : inverse, frame.dim);
    const uint16_t dark = scaled(frame.color, frame.dim >> 4);
    const uint16_t offset = (hash >> 8) % WIDTH;
    const uint16_t block = 8 + ((hash >> 16) & 31);
    const bool blank = (hash & 3) == 0;
    for (uint16_t x = 0; x < WIDTH; x++) {
      *pixel++ = blank ? dark : (((x + offset) / block) & 1) != 0 ? bright : 0;
    }
  }
}

void render_sweep_line(const EffectFrame &frame, uint16_t *line) {
  // A flat band with soft edges: only the edges change as it moves, which keeps transfers short.
  static constexpr int32_t HALF_BAND = 110;
  static constexpr int32_t EDGE = 50;
  static constexpr uint32_t PIXELS_PER_SECOND = 400;
  const int32_t travel = TUBE_CANVAS_PIXELS + 2 * HALF_BAND;
  const int32_t position =
      static_cast<int32_t>(static_cast<uint64_t>(frame.elapsed_ms) * PIXELS_PER_SECOND / 1000 % travel) - HALF_BAND;
  const int32_t tube_left = tube_canvas_left(frame.tube);
  for (uint16_t x = 0; x < WIDTH; x++) {
    const int32_t inside = HALF_BAND - abs(tube_left + x - position);
    const uint16_t level = inside <= 0 ? 0 : inside >= EDGE ? 256 : inside * 256 / EDGE;
    line[x] = scaled(frame.color, (level * frame.dim) >> 8);
  }
}

float pulse_brightness(uint32_t elapsed_ms) {
  static constexpr uint32_t PERIOD_MS = 1600;
  static constexpr float TWO_PI = 6.2831853f;
  const float phase = static_cast<float>(elapsed_ms % PERIOD_MS) / PERIOD_MS;
  return (1.0f - cosf(TWO_PI * phase)) / 2.0f;
}

static void render_pulse(const EffectFrame &frame, uint16_t *strip, uint16_t rows) {
  // Pixel values are gamma encoded: without the exponent the light would change in visible steps near black.
  const uint16_t level = static_cast<uint16_t>(256.0f * powf(pulse_brightness(frame.elapsed_ms), 1.0f / DISPLAY_GAMMA));
  const uint16_t color = scaled(frame.color, (level * frame.dim) >> 8);
  for (uint16_t x = 0; x < WIDTH; x++) {
    strip[x] = color;
  }
  fill_rows_from_first(strip, rows);
}

void render_effect_strip(const EffectFrame &frame, uint16_t *strip, uint16_t first_row, uint16_t rows) {
  switch (frame.effect) {
    case Effect::MATRIX:
      render_matrix(frame, strip, first_row, rows);
      break;
    case Effect::GLITCH:
      render_glitch(frame, strip, first_row, rows);
      break;
    case Effect::PULSE:
      render_pulse(frame, strip, rows);
      break;
    default:
      memset(strip, 0, WIDTH * rows * sizeof(uint16_t));
      break;
  }
}

}  // namespace elekstube_ips
}  // namespace esphome
