#include "animation.h"

#include <cstring>

namespace esphome {
namespace elekstube_ips {

struct EffectInfo {
  Effect effect;
  const char *name;
  Rgb color;
};

// In the order of the "Animation" selector.
static const EffectInfo EFFECTS[] = {
    {Effect::MATRIX, "matrix", {0, 255, 70}},
    {Effect::GLITCH, "glitch", {255, 0, 200}},
    {Effect::SWEEP, "sweep", {0, 200, 255}},
    {Effect::PULSE, "pulse", {255, 40, 0}},
    {Effect::COUNTDOWN, "countdown", {255, 110, 20}},
    {Effect::CLIP, "clip", {255, 255, 255}},
};

Effect effect_from_name(const char *name) {
  for (const EffectInfo &info : EFFECTS) {
    if (strcmp(info.name, name) == 0) {
      return info.effect;
    }
  }
  return Effect::NONE;
}

Effect effect_from_index(size_t index) {
  return index < sizeof(EFFECTS) / sizeof(EFFECTS[0]) ? EFFECTS[index].effect : Effect::NONE;
}

const char *effect_name(Effect effect) {
  for (const EffectInfo &info : EFFECTS) {
    if (info.effect == effect) {
      return info.name;
    }
  }
  return "none";
}

Rgb effect_default_color(Effect effect) {
  for (const EffectInfo &info : EFFECTS) {
    if (info.effect == effect) {
      return info.color;
    }
  }
  return {255, 255, 255};
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool parse_color(const char *text, Rgb *color) {
  if (text[0] != '#' || strlen(text) != 7) {
    return false;
  }
  uint8_t channels[3];
  for (uint8_t channel = 0; channel < 3; channel++) {
    const int high = hex_value(text[1 + channel * 2]);
    const int low = hex_value(text[2 + channel * 2]);
    if (high < 0 || low < 0) {
      return false;
    }
    channels[channel] = static_cast<uint8_t>(high * 16 + low);
  }
  *color = {channels[0], channels[1], channels[2]};
  return true;
}

bool parse_tubes(const char *text, uint8_t *mask) {
  if (strcmp(text, "all") == 0) {
    *mask = 0x3F;
    return true;
  }
  uint8_t result = 0;
  for (const char *c = text; *c != '\0'; c++) {
    if (*c < '1' || *c > '6') {
      return false;
    }
    result |= 1 << (*c - '1');
  }
  if (result == 0) {
    return false;
  }
  *mask = result;
  return true;
}

}  // namespace elekstube_ips
}  // namespace esphome
