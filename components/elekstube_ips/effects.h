// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation.h"

namespace esphome {
namespace elekstube_ips {

struct EffectFrame {
  Effect effect;
  uint8_t tube;
  uint32_t elapsed_ms;
  Rgb color;
  uint16_t dim;  // 0 to 256
};

// Fills `rows` rows of a 135 pixel wide strip in wire format. Every effect is a pure function
// of time, so it needs no memory per tube and can be redrawn from any point.
void render_effect_strip(const EffectFrame &frame, uint16_t *strip, uint16_t first_row, uint16_t rows);

// The sweep is the same on every row, so one 135 pixel line describes a whole tube. Comparing
// lines lets the driver send only the columns that changed.
void render_sweep_line(const EffectFrame &frame, uint16_t *line);

// Emitted light of the pulse, from 0 to 1. The screens and the LEDs share it to stay in step.
float pulse_brightness(uint32_t elapsed_ms);

}  // namespace elekstube_ips
}  // namespace esphome
