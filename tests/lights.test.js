// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
const test = require("node:test");
const assert = require("node:assert");

const lights = require("../site/lights.js");

const dark = (frames) => new Uint8Array(frames * 18);
const at = (track, frame, led) => Array.from(lights.color(track, frame, led));

test("positions from the left become LED numbers, the leftmost being tube 5", () => {
  assert.strictEqual(lights.ledOfPosition(0), 5);
  assert.strictEqual(lights.ledOfPosition(5), 0);
});

test("a flash is added to one LED during its time, and nowhere else", () => {
  const track = dark(10);
  const out = lights.merge(track, 10, 10, [{ tube: 0, start: 0.2, length: 0.3, color: [200, 100, 50], strength: 1 }]);
  assert.deepStrictEqual(at(out, 2, 5), [200, 100, 50]);
  assert.deepStrictEqual(at(out, 4, 5), [200, 100, 50]);
  assert.deepStrictEqual(at(out, 1, 5), [0, 0, 0]);
  assert.deepStrictEqual(at(out, 5, 5), [0, 0, 0]);
  assert.deepStrictEqual(at(out, 3, 0), [0, 0, 0]);
  assert.deepStrictEqual(Array.from(track), Array.from(dark(10)), "the original is not changed");
});

test("all tubes light together, and a flash adds up to 255 at most", () => {
  const track = new Uint8Array(3 * 18).fill(200);
  const out = lights.merge(track, 3, 10, [{ tube: "all", start: 0, length: 0.1, color: [100, 0, 0], strength: 1 }]);
  assert.deepStrictEqual([0, 1, 2, 3, 4, 5].map((led) => at(out, 0, led)[0]), [255, 255, 255, 255, 255, 255]);
  assert.deepStrictEqual(at(out, 0, 3), [255, 200, 200]);
  assert.deepStrictEqual(at(out, 1, 3), [200, 200, 200]);
});

test("replacing takes the place of the picture in proportion to its strength", () => {
  const track = new Uint8Array(2 * 18).fill(100);
  const out = lights.merge(track, 2, 10, [{ tube: 5, start: 0, length: 0.2, color: [0, 0, 0], strength: 0.5 }], true);
  assert.deepStrictEqual(at(out, 0, 0), [50, 50, 50]);
  assert.deepStrictEqual(at(out, 0, 1), [100, 100, 100]);
});

test("a fade makes the flash rise and fall", () => {
  const out = lights.merge(dark(20), 20, 10, [{ tube: 0, start: 0, length: 2, color: [255, 0, 0], strength: 1, fade: 0.5 }]);
  const red = Array.from({ length: 20 }, (_, frame) => at(out, frame, 5)[0]);
  assert.ok(red[0] < red[2] && red[2] < red[4], "rises");
  assert.strictEqual(red[10], 255);
  assert.ok(red[19] < red[16], "falls");
});

test("a flash outside the animation, or of no length, does nothing", () => {
  const track = dark(5);
  const out = lights.merge(track, 5, 10, [
    { tube: "all", start: 9, length: 1, color: [255, 255, 255], strength: 1 },
    { tube: "all", start: 0.2, length: 0, color: [255, 255, 255], strength: 1 },
  ]);
  assert.deepStrictEqual(Array.from(out), Array.from(track));
});

test("scale makes the lights brighter or dimmer and keeps them in range", () => {
  assert.deepStrictEqual(Array.from(lights.scale(new Uint8Array([100, 200, 0]), 2)), [200, 255, 0]);
  assert.deepStrictEqual(Array.from(lights.scale(new Uint8Array([100, 200, 0]), 0.5)), [50, 100, 0]);
});
