// Run with: node --test tests/
// Checks the store library of the web page against stores that Python wrote, so the two stay the same format.
const test = require("node:test");
const assert = require("node:assert");
const { execFileSync } = require("node:child_process");
const path = require("node:path");

const store = require("../web/store.js");

const root = path.join(__dirname, "..");
const python = process.env.PYTHON || "python";

// A store with a face, a plain clip, a six image clip and a scene, made by the Python builder.
function pythonStore() {
  const script = `
import sys
sys.path.insert(0, "tools")
import make_clips as c, sys
digits = [bytes([d]) * (d + 3) for d in range(10)]
scene = c.Scene(bytes(512), [b"\\x00\\x00", b"\\x00\\x00"], [])
lib = c.build_library(
    [("plain", 15, [[b"aaaa"], [b"bbbb"], [b"aaaa"]]), ("six", 4, [[bytes([i]) for i in range(6)]]), ("scene", 15, scene)],
    [("Neon", digits)])
sys.stdout.buffer.write(lib)`;
  return new Uint8Array(execFileSync(python, ["-I", "-c", script], { cwd: root, maxBuffer: 1 << 24 }));
}

test("reads a store that Python wrote", () => {
  const { entries, size } = store.parse(pythonStore());
  assert.deepStrictEqual(entries.map((e) => [e.name, e.kind]), [["Neon", 1], ["plain", 0], ["six", 0], ["scene", 0]]);
  assert.strictEqual(entries[0].images.length, 10);
  assert.deepStrictEqual(Array.from(entries[0].images[3]), [3, 3, 3, 3, 3, 3]);
  assert.strictEqual(entries[1].frames, 3);
  assert.strictEqual(entries[1].images.length, 3);
  assert.strictEqual(entries[3].imagesPerFrame, store.SCENE);
  assert.strictEqual(entries[3].images.length, 1 + 2 + 0);
  assert.ok(size > 0);
});

test("writes the same bytes that Python does", () => {
  const original = pythonStore();
  const rebuilt = store.build(store.parse(original).entries);
  assert.strictEqual(Buffer.compare(Buffer.from(original), Buffer.from(rebuilt)), 0);
});

test("adds and removes entries", () => {
  const { entries } = store.parse(pythonStore());
  const digits = Array.from({ length: 10 }, (_, d) => new Uint8Array([200 + d, 1, 2]));
  const added = store.parse(store.build([...entries, store.face("Marble", digits)])).entries;
  assert.deepStrictEqual(added.map((e) => e.name), ["Neon", "plain", "six", "scene", "Marble"]);
  assert.deepStrictEqual(Array.from(added[4].images[9]), [209, 1, 2]);
  const removed = store.parse(store.build(entries.filter((e) => e.name !== "plain"))).entries;
  assert.deepStrictEqual(removed.map((e) => e.name), ["Neon", "six", "scene"]);
});

test("reads and writes a theme", () => {
  const script = `
import sys
sys.path.insert(0, "tools")
import make_clips as c
lib = c.build_library([("boot", 15, [[b"aaaa"]])], theme=("Space", {"author": "A", "license": "MIT"}))
sys.stdout.buffer.write(lib)`;
  const original = new Uint8Array(execFileSync(python, ["-I", "-c", script], { cwd: root, maxBuffer: 1 << 24 }));
  const { entries } = store.parse(original);
  const theme = entries.find((entry) => entry.kind === store.KIND_THEME);
  assert.strictEqual(theme.name, "Space");
  assert.deepStrictEqual(store.themeInfo(theme), { author: "A", license: "MIT" });
  assert.strictEqual(Buffer.compare(Buffer.from(original), Buffer.from(store.build(entries))), 0);
  const made = store.parse(store.build([store.theme("Cats", { author: "B" }), entries[1]])).entries;
  assert.deepStrictEqual(made.map((entry) => entry.name), ["Cats", "boot"]);
});

test("keeps the lights of a scene through a rebuild", () => {
  const script = `
import sys
sys.path.insert(0, "tools")
import make_clips as c
scene = c.Scene(bytes(512), [bytes(2), bytes(2)], [], bytes(range(36)))
sys.stdout.buffer.write(c.build_library([("scene", 15, scene)]))`;
  const original = new Uint8Array(execFileSync(python, ["-I", "-c", script], { cwd: root, maxBuffer: 1 << 24 }));
  const { entries } = store.parse(original);
  assert.strictEqual(entries[0].reserved & 1, 1);
  assert.strictEqual(entries[0].images.length, 1 + 2 + 0 + 1);
  assert.deepStrictEqual(Array.from(entries[0].images[3]), Array.from({ length: 36 }, (_, i) => i));
  assert.strictEqual(Buffer.compare(Buffer.from(original), Buffer.from(store.build(entries))), 0);
});

test("limits the faces and the themes", () => {
  const digits = Array.from({ length: 10 }, () => new Uint8Array([1]));
  const faces = (count) => Array.from({ length: count }, (_, i) => store.face(`Face${i}`, digits));
  store.build(faces(store.MAX_FACES));
  assert.throws(() => store.build(faces(store.MAX_FACES + 1)), /at most 4 faces/);
  assert.throws(() => store.build([store.theme("A", {}), store.theme("B", {})]), /one theme/);
});

test("identical images are stored once", () => {
  const image = new Uint8Array(1000).fill(7);
  const clip = { name: "loop", kind: 0, frames: 10, fps: 15, imagesPerFrame: 1, sceneBlobs: 0, images: Array(10).fill(image) };
  assert.ok(store.build([clip]).length < 1200);
});

test("rejects what the clock would reject", () => {
  const good = pythonStore();
  assert.throws(() => store.parse(good.subarray(0, 100)), /header says/);
  assert.throws(() => store.parse(new Uint8Array(50)), /not a store/);
  const damaged = good.slice();
  damaged[damaged.length - 5] ^= 0xff;
  assert.throws(() => store.parse(damaged), /damaged/);
  const other = good.slice();
  other[4] = 9;
  assert.throws(() => store.parse(other), /version/);
  const { entries } = store.parse(good);
  assert.throws(() => store.build([]), /nothing/);
  assert.throws(() => store.build([entries[0], entries[0]]), /twice/);
  assert.throws(() => store.build([{ ...entries[0], name: "a name that is far too long" }]), /invalid name/);
  assert.throws(() => store.build(entries, 100), /room/);
  assert.throws(() => store.face("x", []), /ten/);
});
