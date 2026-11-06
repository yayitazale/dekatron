// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
const test = require("node:test");
const assert = require("node:assert");

const color = require("../web/colorpicker.js");

test("hex and hsv turn into each other", () => {
  for (const hex of ["#000000", "#ffffff", "#ff0000", "#00ff00", "#0000ff", "#ffb020", "#3fb6ff", "#8b7bff", "#123456", "#fedcba"]) {
    const [h, s, v] = color.hexToHsv(hex);
    assert.strictEqual(color.hsvToHex(h, s, v), hex, hex);
  }
});

test("the primary colors have their hues, and grays have no saturation", () => {
  assert.deepStrictEqual(color.hexToHsv("#ff0000"), [0, 1, 1]);
  assert.strictEqual(Math.round(color.hexToHsv("#00ff00")[0]), 120);
  assert.strictEqual(Math.round(color.hexToHsv("#0000ff")[0]), 240);
  assert.strictEqual(color.hexToHsv("#808080")[1], 0);
});

test("only #rrggbb is a color", () => {
  assert.ok(color.isHex("#a1B2c3"));
  assert.ok(!color.isHex("a1b2c3"));
  assert.ok(!color.isHex("#a1b2c"));
  assert.ok(!color.isHex("#a1b2c3d"));
});

test("every preset is a color", () => {
  assert.ok(color.PRESETS.every(color.isHex));
});
