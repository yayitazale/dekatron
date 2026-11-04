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

struct AnimationRequest {
  Effect effect{Effect::NONE};
  uint8_t tube_mask{0x3F};
  uint8_t priority{DEFAULT_PRIORITY};
  uint32_t duration_ms{10000};
  Rgb color{0, 0, 0};
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
