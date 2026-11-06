// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// checkBin of the page says the same as tools/check_bin.py about the same files.
const test = require("node:test");
const assert = require("node:assert");
const { execFileSync } = require("node:child_process");
const path = require("node:path");

const store = require("../web/store.js");

const root = path.join(__dirname, "..");
const python = process.env.PYTHON || "python";
const cases = JSON.parse(execFileSync(python, ["-I", path.join("tests", "check_cases.py")], { cwd: root, maxBuffer: 1 << 28 }).toString());

for (const [name, expected] of Object.entries(cases)) {
  test(`checkBin agrees with Python about ${name}`, () => {
    const report = store.checkBin(Buffer.from(expected.hex, "hex"), { gallery: expected.gallery });
    assert.deepStrictEqual(
      { type: report.type, problems: report.problems.map((p) => p.rule), warnings: report.warnings.map((w) => w.rule) },
      { type: expected.type, problems: expected.problems, warnings: expected.warnings },
    );
    assert.strictEqual(report.ok, expected.problems.length === 0);
  });
}

test("kindOf tells a face, a theme, a backup and a firmware, and refuses anything else", () => {
  const kind = (name) => store.kindOf(Buffer.from(cases[name].hex, "hex"));
  assert.deepStrictEqual(["good-face", "good-theme", "backup", "firmware"].map(kind), ["face", "theme", "backup", "firmware"]);
  assert.throws(() => kind("not-a-store"));
});

test("a face keeps its information, and a face without one has none", () => {
  const digits = Array.from({ length: 10 }, (_, d) => new Uint8Array([d, 1]));
  const withInfo = store.face("Neon", digits, { author: "A", license: "CC-BY-SA-4.0" });
  assert.strictEqual(withInfo.imagesPerFrame, 11);
  const [read] = store.parse(store.build([withInfo])).entries;
  assert.deepStrictEqual(store.faceInfo(read), { author: "A", license: "CC-BY-SA-4.0" });
  assert.deepStrictEqual(store.faceInfo(store.face("Plain", digits)), {});
});
