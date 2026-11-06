// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace elekstube_ips {

enum class Effect : uint8_t {
  NONE = 0,
  MATRIX,
  GLITCH,
  SWEEP,
  PULSE,
  COUNTDOWN,
  CLIP,
};

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Lower numbers are more urgent, so 1 interrupts 2 and 2 replaces another 2.
static constexpr uint8_t DEFAULT_PRIORITY = 2;
static constexpr uint8_t LOWEST_PRIORITY = 9;
static constexpr uint32_t MAX_DURATION_MS = 3600000;
static constexpr float DISPLAY_GAMMA = 2.2f;

// Shapes moving across the tubes are laid out on a canvas that includes the physical gap between
// screens, so they pass behind it instead of jumping. Adjusted by eye on the clock, starting from
// a photo where the pitch was about 1.95 screen widths.
static constexpr int32_t TUBE_GAP_PIXELS = 112;
static constexpr int32_t TUBE_PITCH_PIXELS = 135 + TUBE_GAP_PIXELS;
static constexpr int32_t TUBE_CANVAS_PIXELS = 5 * TUBE_PITCH_PIXELS + 135;

// Tube 5 is the leftmost one.
inline int32_t tube_canvas_left(uint8_t tube) { return (5 - tube) * TUBE_PITCH_PIXELS; }

struct AnimationRequest {
  Effect effect{Effect::NONE};
  uint8_t tube_mask{0x3F};
  uint8_t priority{DEFAULT_PRIORITY};
  uint32_t duration_ms{10000};
  Rgb color{0, 0, 0};
  bool control_leds{true};
  uint8_t clip_index{0};
};

Effect effect_from_name(const char *name);
Effect effect_from_index(size_t index);
const char *effect_name(Effect effect);
Rgb effect_default_color(Effect effect);

bool parse_color(const char *text, Rgb *color);
// Accepts "all" or the tube numbers as digits, from "1" (seconds units) to "6" (hours tens).
bool parse_tubes(const char *text, uint8_t *mask);

inline bool priority_allows(uint8_t incoming, uint8_t active) { return incoming <= active; }

}  // namespace elekstube_ips
}  // namespace esphome
