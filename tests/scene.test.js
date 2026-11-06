// Run with: node --test tests/
// Checks the scene encoder of the page against the Python one, which the firmware is tested with.
const test = require("node:test");
const assert = require("node:assert");
const { execFileSync } = require("node:child_process");
const path = require("node:path");

const scene = require("../site/scene.js");

const root = path.join(__dirname, "..");
const python = process.env.PYTHON || "python";

// Frames drawn by Python, and what it makes of them: the encoding, and the lights with and without a background.
function pythonScene() {
  const script = `
import base64, json, random, sys
sys.path.insert(0, "tools")
from PIL import Image, ImageDraw
import make_clips as c, make_scenes as m
random.seed(7)
entries = [(0, 0, 0), (248, 0, 0), (0, 252, 0), (0, 0, 248), (248, 252, 0), (248, 0, 248)] + [(0, 0, 0)] * 250
colors = entries[1:6]
background = Image.new("RGB", (c.CANVAS_WIDTH, c.HEIGHT), (0, 0, 0))
ImageDraw.Draw(background).rectangle([0, 200, c.CANVAS_WIDTH, 239], fill=(0, 0, 248))
frames = []
for step in range(6):
    frame = background.copy()
    draw = ImageDraw.Draw(frame)
    draw.rectangle([30 + step * 150, 40, 90 + step * 150, 130], fill=colors[step % 5])
    draw.ellipse([700 - step * 20, 60 + step * 5, 760 - step * 20, 120 + step * 5], fill=colors[(step + 2) % 5])
    for _ in range(40):
        x, y = random.randrange(c.CANVAS_WIDTH - 5), random.randrange(c.HEIGHT - 5)
        draw.rectangle([x, y, x + random.randrange(1, 5), y + random.randrange(1, 5)], fill=random.choice(colors))
    frames.append(frame)
encoded = m.encode_scene(frames, entries=entries, background=None, lights=True)
with_background = m.led_track(frames, background)
hexed = lambda b: bytes(b).hex()
print(json.dumps({
    "entries": entries,
    "frames": [base64.b64encode(f.tobytes()).decode() for f in frames],
    "background": base64.b64encode(background.tobytes()).decode(),
    "palette": hexed(encoded.palette),
    "records": [hexed(r) for r in encoded.records],
    "blobs": [hexed(b) for b in encoded.blobs],
    "leds": hexed(encoded.leds),
    "ledsBackground": hexed(with_background),
    "rle": [[list(d), hexed(m.rle(bytes(d)))] for d in ([], [1], [1, 1, 1, 1], [0] * 300, list(range(200)), [1, 2] * 100 + [3] * 50)],
}))`;
  return JSON.parse(execFileSync(python, ["-I", "-c", script], { cwd: root, maxBuffer: 1 << 28 }).toString());
}

const imageData = (base64) => {
  const rgb = Buffer.from(base64, "base64");
  const data = new Uint8ClampedArray((rgb.length / 3) * 4);
  for (let i = 0, j = 0; i < rgb.length; i += 3, j += 4) {
    data[j] = rgb[i];
    data[j + 1] = rgb[i + 1];
    data[j + 2] = rgb[i + 2];
    data[j + 3] = 255;
  }
  return { data, width: scene.CANVAS_WIDTH, height: scene.HEIGHT };
};
const hex = (bytes) => Buffer.from(bytes).toString("hex");

const reference = pythonScene();
const frames = reference.frames.map(imageData);

test("run length encoding is the same", () => {
  for (const [data, expected] of reference.rle) assert.strictEqual(hex(scene.rle(data)), expected);
});

test("a scene is encoded to the same bytes", () => {
  const quantize = scene.makeQuantizer(reference.entries);
  const encoded = scene.encodeIndexed(frames.map(quantize), reference.entries);
  assert.strictEqual(hex(encoded.palette), reference.palette);
  assert.deepStrictEqual(encoded.records.map(hex), reference.records);
  assert.deepStrictEqual(encoded.blobs.map(hex), reference.blobs);
});

function closeTo(actual, expectedHex, tolerance) {
  const expected = Buffer.from(expectedHex, "hex");
  assert.strictEqual(actual.length, expected.length);
  let worst = 0;
  for (let i = 0; i < expected.length; i++) worst = Math.max(worst, Math.abs(actual[i] - expected[i]));
  assert.ok(worst <= tolerance, `the lights differ by ${worst}`);
}

test("the lights follow the picture like in Python", () => {
  closeTo(scene.ledTrack(frames), reference.leds, 2);
});

test("the lights follow what moves over a background like in Python", () => {
  closeTo(scene.ledTrack(frames, imageData(reference.background)), reference.ledsBackground, 2);
});

test("a palette from frames holds black first and what the frames show", () => {
  const entries = scene.buildPalette(frames);
  assert.strictEqual(entries.length, 256);
  assert.deepStrictEqual(entries[0], [0, 0, 0]);
  const quantize = scene.makeQuantizer(entries);
  const indexed = quantize(frames[2]);
  let worst = 0;
  for (let i = 0; i < indexed.length; i += 97) {
    const [r, g, b] = entries[indexed[i]];
    worst = Math.max(worst, Math.abs(r - frames[2].data[i * 4]), Math.abs(g - frames[2].data[i * 4 + 1]), Math.abs(b - frames[2].data[i * 4 + 2]));
  }
  assert.ok(worst <= 8, `a color is off by ${worst}`);
});

test("a scene becomes an entry that the store accepts", () => {
  const store = require("../web/store.js");
  const quantize = scene.makeQuantizer(reference.entries);
  const encoded = scene.encodeIndexed(frames.map(quantize), reference.entries, scene.ledTrack(frames));
  const clip = scene.toClip("boot", 15, encoded);
  const parsed = store.parse(store.build([clip]));
  assert.strictEqual(parsed.entries[0].frames, frames.length);
  assert.strictEqual(parsed.entries[0].reserved & 1, 1);
});
