// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
const test = require("node:test");
const assert = require("node:assert");
const crypto = require("node:crypto");

const update = require("../web/update.js");

test("compares versions as numbers", () => {
  assert.strictEqual(update.compare("0.2.0", "0.1.9"), 1);
  assert.strictEqual(update.compare("0.10.0", "0.9.0"), 1);
  assert.strictEqual(update.compare("v1.0.0", "1.0.0"), 0);
  assert.strictEqual(update.compare("0.1.0", "0.1.1"), -1);
  assert.strictEqual(update.compare("dev", "0.1.1"), null);
});

test("sha256 matches the one of node for sizes around the blocks", () => {
  for (const size of [0, 1, 55, 56, 63, 64, 65, 119, 120, 1000, 100000]) {
    const bytes = crypto.randomBytes(size);
    assert.strictEqual(update.sha256(new Uint8Array(bytes)), crypto.createHash("sha256").update(bytes).digest("hex"), `${size} bytes`);
  }
});

const good = { latest: "0.2.0", date: "2026-11-20", notes: "Faster uploads", firmware: { full: { url: "https://example.org/a.ota.bin", sha256: "a".repeat(64) } } };

test("offers only a newer release with a safe download", () => {
  assert.deepStrictEqual(update.pick(good, "0.1.0"), { version: "0.2.0", date: "2026-11-20", notes: "Faster uploads", url: "https://example.org/a.ota.bin", sha256: "a".repeat(64) });
  assert.strictEqual(update.pick(good, "0.2.0"), null);
  assert.strictEqual(update.pick(good, "0.3.0"), null);
  assert.strictEqual(update.pick(good, "unknown"), null);
  assert.strictEqual(update.pick(good, "0.1.0", "clock"), null);
  assert.strictEqual(update.pick({ ...good, firmware: { full: { url: "http://example.org/a.bin", sha256: "a".repeat(64) } } }, "0.1.0"), null);
  assert.strictEqual(update.pick({ ...good, firmware: { full: { url: "https://example.org/a.bin", sha256: "xyz" } } }, "0.1.0"), null);
  assert.strictEqual(update.pick(null, "0.1.0"), null);
});

test("asks again after two hours, and when the clock of the device went back", () => {
  const now = 1_000_000_000_000;
  assert.strictEqual(update.due(0, now), true);
  assert.strictEqual(update.due(now - 3600_000, now), false);
  assert.strictEqual(update.due(now - 7300_000, now), true);
  assert.strictEqual(update.due(now + 5000, now), true);
});
