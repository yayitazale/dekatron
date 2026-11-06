// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes for the page, the way tools/make_scenes.py makes them: an animation that crosses the six tubes,
// stored as only the rectangles that change between frames, with a color for each light on every frame.
// tests/scene.test.js checks this against the Python code, so the two write the same bytes.
const DekatronScene = (() => {
  const WIDTH = 135;
  const HEIGHT = 240;
  const TUBE_COUNT = 6;
  const TUBE_PITCH = WIDTH + 112; // must match TUBE_GAP_PIXELS in animation.h
  const CANVAS_WIDTH = (TUBE_COUNT - 1) * TUBE_PITCH + WIDTH;
  const TILE = 8;

  // Token below 128: repeat the next byte token + 1 times. Otherwise token - 127 literal bytes.
  function rle(indices) {
    const output = [];
    const literal = [];
    const flush = () => {
      if (literal.length) {
        output.push(127 + literal.length, ...literal);
        literal.length = 0;
      }
    };
    let position = 0;
    const length = indices.length;
    while (position < length) {
      const value = indices[position];
      let run = 1;
      while (position + run < length && run < 128 && indices[position + run] === value) run++;
      if (run >= 3) {
        flush();
        output.push(run - 1, value);
        position += run;
      } else {
        literal.push(value);
        position++;
        if (literal.length === 128) flush();
      }
    }
    flush();
    return Uint8Array.from(output);
  }

  const sameRange = (a, b, from, to) => {
    for (let i = from; i < to; i++) if (a[i] !== b[i]) return false;
    return true;
  };

  // Rectangles [x, y, width, height] that cover the changes between two tube images.
  function dirtyRectangles(before, after) {
    const out = [];
    let spans = new Map();
    const key = (start, end) => `${start},${end}`;
    const emit = (map, bottom) => {
      for (const [k, top] of map) {
        const [start, end] = k.split(",").map(Number);
        out.push([start * TILE, top, Math.min(end * TILE, WIDTH) - start * TILE, bottom - top]);
      }
    };
    for (let tileRow = 0; tileRow < HEIGHT; tileRow += TILE) {
      const rowBottom = Math.min(tileRow + TILE, HEIGHT);
      const dirty = [];
      for (let tileColumn = 0; tileColumn < WIDTH; tileColumn += TILE) {
        const width = Math.min(TILE, WIDTH - tileColumn);
        let changed = false;
        for (let y = tileRow; y < rowBottom && !changed; y++) {
          changed = !sameRange(before, after, y * WIDTH + tileColumn, y * WIDTH + tileColumn + width) ? true : false;
        }
        dirty.push(changed);
      }
      let column = 0;
      const current = new Map();
      while (column < dirty.length) {
        if (!dirty[column]) {
          column++;
          continue;
        }
        const start = column;
        while (column < dirty.length && dirty[column]) column++;
        const k = key(start, column);
        const top = spans.has(k) ? spans.get(k) : tileRow;
        spans.delete(k);
        current.set(k, top);
      }
      emit(spans, tileRow);
      spans = current;
    }
    emit(spans, HEIGHT);
    return out;
  }

  // Joins rectangles that touch, so one shape is always cut the same way.
  function mergeTouching(rectangles) {
    const merged = rectangles.map((rectangle) => rectangle.slice());
    let changed = true;
    while (changed) {
      changed = false;
      outer: for (let first = 0; first < merged.length; first++) {
        for (let second = first + 1; second < merged.length; second++) {
          const [ax, ay, aw, ah] = merged[first];
          const [bx, by, bw, bh] = merged[second];
          if (ax <= bx + bw && bx <= ax + aw && ay <= by + bh && by <= ay + ah) {
            const left = Math.min(ax, bx);
            const top = Math.min(ay, by);
            merged[first] = [left, top, Math.max(ax + aw, bx + bw) - left, Math.max(ay + ah, by + bh) - top];
            merged.splice(second, 1);
            changed = true;
            break outer;
          }
        }
      }
    }
    return merged;
  }

  // Splits a changed rectangle into the tight box of what is lit and the black around it, so a shape on
  // black gives the same pixels wherever it is, and its frames can be shared.
  function splitForeground(before, after, x, y, width, height) {
    const out = [];
    const lit = [];
    const first = new Map();
    const last = new Map();
    for (let row = 0; row < height; row++) {
      const start = (y + row) * WIDTH + x;
      let left = -1;
      let right = -1;
      for (let i = 0; i < width; i++) {
        if (after[start + i] !== 0) {
          if (left < 0) left = i;
          right = i + 1;
        }
      }
      if (left >= 0) {
        lit.push(row);
        first.set(row, left);
        last.set(row, right);
      }
    }
    if (!lit.length) return [[x, y, width, height]];
    const top = lit[0];
    const bottom = lit[lit.length - 1] + 1;
    const left = Math.min(...lit.map((row) => first.get(row)));
    const right = Math.max(...lit.map((row) => last.get(row)));
    const wasLit = (bx, by, bw, bh) => {
      for (let row = by; row < by + bh; row++) {
        for (let i = bx; i < bx + bw; i++) if (before[row * WIDTH + i] !== 0) return true;
      }
      return false;
    };
    const around = [
      [x, y, width, top],
      [x, y + bottom, width, height - bottom],
      [x, y + top, left, bottom - top],
      [x + right, y + top, width - right, bottom - top],
    ];
    for (const box of around) if (box[2] > 0 && box[3] > 0 && wasLit(...box)) out.push(box);
    out.push([x + left, y + top, right - left, bottom - top]);
    return out;
  }

  const to565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  const from565 = (value) => {
    const r = (value >> 11) & 31;
    const g = (value >> 5) & 63;
    const b = value & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
  };

  // The palette that the scenes of a theme share, found by median cut on what the tubes can show.
  // `samples` are frames (ImageData); entry 0 is black and the rest are padded with black.
  function buildPalette(samples, colors = 255) {
    const counts = new Map();
    for (const frame of samples) {
      const data = frame.data;
      for (let i = 0; i < data.length; i += 4) {
        const key = to565(data[i], data[i + 1], data[i + 2]);
        counts.set(key, (counts.get(key) || 0) + 1);
      }
    }
    let boxes = [[...counts].map(([key, count]) => ({ rgb: from565(key), count }))];
    while (boxes.length < colors) {
      let best = -1;
      let bestScore = 0;
      let bestAxis = 0;
      boxes.forEach((box, index) => {
        if (box.length < 2) return;
        for (let axis = 0; axis < 3; axis++) {
          let low = 255;
          let high = 0;
          for (const color of box) {
            low = Math.min(low, color.rgb[axis]);
            high = Math.max(high, color.rgb[axis]);
          }
          const score = (high - low) * Math.sqrt(box.reduce((sum, color) => sum + color.count, 0));
          if (score > bestScore) {
            bestScore = score;
            best = index;
            bestAxis = axis;
          }
        }
      });
      if (best < 0) break;
      const box = boxes[best].slice().sort((a, b) => a.rgb[bestAxis] - b.rgb[bestAxis]);
      const half = box.reduce((sum, color) => sum + color.count, 0) / 2;
      let running = 0;
      let cut = 1;
      for (let i = 0; i < box.length - 1; i++) {
        running += box[i].count;
        cut = i + 1;
        if (running >= half) break;
      }
      boxes.splice(best, 1, box.slice(0, cut), box.slice(cut));
    }
    const found = boxes.map((box) => {
      const total = box.reduce((sum, color) => sum + color.count, 0) || 1;
      return [0, 1, 2].map((axis) => Math.round(box.reduce((sum, color) => sum + color.rgb[axis] * color.count, 0) / total));
    });
    const entries = [[0, 0, 0], ...found];
    while (entries.length < 256) entries.push([0, 0, 0]);
    return entries.slice(0, 256);
  }

  // Turns a frame into the index of the nearest palette color for every pixel. A color is only looked
  // for once, as the tubes show 16 bits of color anyway.
  function makeQuantizer(entries) {
    const lookup = new Int16Array(65536).fill(-1);
    const exact = new Map();
    entries.forEach((entry, index) => {
      const key = to565(...entry);
      if (!exact.has(key)) exact.set(key, index);
    });
    const nearest = (key) => {
      if (exact.has(key)) return exact.get(key);
      const [r, g, b] = from565(key);
      let best = 0;
      let bestDistance = Infinity;
      for (let i = 0; i < entries.length; i++) {
        const dr = entries[i][0] - r;
        const dg = entries[i][1] - g;
        const db = entries[i][2] - b;
        const distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
          bestDistance = distance;
          best = i;
        }
      }
      return best;
    };
    // Every black entry is index 0, which is what the tubes show at the start.
    const black = entries.map((entry) => (entry[0] === 0 && entry[1] === 0 && entry[2] === 0 ? 0 : -1));
    return (frame) => {
      const data = frame.data;
      const out = new Uint8Array(data.length / 4);
      for (let i = 0, p = 0; i < data.length; i += 4, p++) {
        const key = to565(data[i], data[i + 1], data[i + 2]);
        let index = lookup[key];
        if (index < 0) {
          index = nearest(key);
          if (black[index] === 0) index = 0;
          lookup[key] = index;
        }
        out[p] = index;
      }
      return out;
    };
  }

  const paletteBytes = (entries) => {
    const out = new Uint8Array(512);
    entries.forEach(([r, g, b], i) => {
      const value = to565(r, g, b);
      out[i * 2] = value & 255;
      out[i * 2 + 1] = value >> 8;
    });
    return out;
  };

  // What a region of a picture asks of a light: the mean color of what is lit, as strong as the region is
  // full of it, and dimmer when that is dark. `sum` holds the sums of the channels, `lit` the pixels
  // brighter than a little, and `n` the pixels.
  function regionLight(sum, lit, n) {
    const mean = sum.map((channel) => Math.round(channel / n));
    const litFraction = Math.round((lit * 255) / n) / 255;
    if (litFraction < 0.002) return [0, 0, 0];
    const litColor = mean.map((channel) => Math.min(255, channel / litFraction));
    const peak = Math.max(...litColor) || 1;
    const strength = Math.min(1, Math.sqrt(litFraction) * 2.2) * (0.4 + (0.6 * peak) / 255);
    return litColor.map((channel) => (channel * 255 * strength) / peak);
  }

  // Sums of one tube of a frame, or of its difference with another one.
  function tubeLight(frame, tube, other) {
    const left = (TUBE_COUNT - 1 - tube) * TUBE_PITCH;
    const sum = [0, 0, 0];
    let lit = 0;
    for (let y = 0; y < HEIGHT; y++) {
      let at = (y * CANVAS_WIDTH + left) * 4;
      for (let x = 0; x < WIDTH; x++, at += 4) {
        let r = frame.data[at];
        let g = frame.data[at + 1];
        let b = frame.data[at + 2];
        if (other) {
          r = Math.abs(r - other.data[at]);
          g = Math.abs(g - other.data[at + 1]);
          b = Math.abs(b - other.data[at + 2]);
        }
        sum[0] += r;
        sum[1] += g;
        sum[2] += b;
        if (((19595 * r + 38470 * g + 7471 * b + 0x8000) >> 16) > 20) lit++;
      }
    }
    return [sum, lit, WIDTH * HEIGHT];
  }

  // One color per LED for a frame, from the picture of its tube. The LED of a tube has the number of the
  // tube, and the pictures run from the leftmost tube, which is tube 5. With a background (what stays still
  // in the scene) the lights follow what moves over it, and it adds only a little.
  // The follower takes the frames one by one and gives the colors of the six lights for each.
  function ledFollower(background = null, ambient = 0.12, smoothing = 0.55) {
    let previous = Array.from({ length: TUBE_COUNT }, () => [0, 0, 0]);
    return (frame) => {
      const out = new Uint8Array(TUBE_COUNT * 3);
      const tubes = [];
      for (let tube = 0; tube < TUBE_COUNT; tube++) {
        let color;
        if (!background) {
          color = regionLight(...tubeLight(frame, tube));
        } else {
          const moving = regionLight(...tubeLight(frame, tube, background));
          const still = regionLight(...tubeLight(frame, tube));
          color = moving.map((value, i) => Math.min(255, value + ambient * still[i]));
        }
        tubes.push(color.map((now, i) => smoothing * now + (1 - smoothing) * previous[tube][i]));
      }
      previous = tubes;
      tubes.forEach((color, tube) => color.forEach((channel, i) => { out[tube * 3 + i] = Math.round(channel); }));
      return out;
    };
  }

  function ledTrack(frames, background = null, ambient = 0.12, smoothing = 0.55) {
    const follow = ledFollower(background, ambient, smoothing);
    const track = new Uint8Array(frames.length * TUBE_COUNT * 3);
    frames.forEach((frame, index) => track.set(follow(frame), index * TUBE_COUNT * 3));
    return track;
  }

  // Encodes frames that are already indexes into the palette, each CANVAS_WIDTH x HEIGHT, from black tubes,
  // one at a time with add(), and finish() gives the scene.
  function sceneEncoder(entries) {
    const shown = Array.from({ length: TUBE_COUNT }, () => new Uint8Array(WIDTH * HEIGHT));
    const blobs = [];
    const places = new Map();
    const records = [];
    const same = (a, b) => a.length === b.length && sameRange(a, b, 0, a.length);
    const blobNumber = (blob) => {
      let hash = 0x811c9dc5;
      for (let i = 0; i < blob.length; i++) hash = Math.imul(hash ^ blob[i], 0x01000193) >>> 0;
      const key = `${hash}:${blob.length}`;
      const candidates = places.get(key) || [];
      for (const candidate of candidates) if (same(candidate.blob, blob)) return candidate.number;
      const number = blobs.length;
      blobs.push(blob);
      candidates.push({ blob, number });
      places.set(key, candidates);
      return number;
    };
    const add = (indexed) => {
      const patches = [];
      let count = 0;
      for (let imageIndex = 0; imageIndex < TUBE_COUNT; imageIndex++) {
        const left = imageIndex * TUBE_PITCH;
        const current = new Uint8Array(WIDTH * HEIGHT);
        for (let y = 0; y < HEIGHT; y++) {
          current.set(indexed.subarray(y * CANVAS_WIDTH + left, y * CANVAS_WIDTH + left + WIDTH), y * WIDTH);
        }
        if (same(current, shown[imageIndex])) continue;
        for (const rectangle of mergeTouching(dirtyRectangles(shown[imageIndex], current))) {
          for (const [x, y, width, height] of splitForeground(shown[imageIndex], current, ...rectangle)) {
            const pixels = new Uint8Array(width * height);
            for (let row = 0; row < height; row++) {
              pixels.set(current.subarray((y + row) * WIDTH + x, (y + row) * WIDTH + x + width), row * width);
            }
            const number = blobNumber(rle(pixels));
            // Images run left to right, and tube 5 is the leftmost one.
            patches.push(TUBE_COUNT - 1 - imageIndex, x, y, width, height, 0, number & 255, number >> 8);
            count++;
          }
        }
        shown[imageIndex] = current;
      }
      const record = new Uint8Array(2 + patches.length);
      record[0] = count & 255;
      record[1] = count >> 8;
      record.set(patches, 2);
      records.push(record);
    };
    const size = () => blobs.reduce((sum, blob) => sum + blob.length, 0) + records.reduce((sum, record) => sum + record.length, 0);
    const finish = (leds = null) => ({ palette: paletteBytes(entries), records, blobs, leds: leds || new Uint8Array(0) });
    return { add, finish, size };
  }

  function encodeIndexed(indexedFrames, entries, leds = null) {
    const encoder = sceneEncoder(entries);
    for (const indexed of indexedFrames) encoder.add(indexed);
    return encoder.finish(leds);
  }

  // The entry of a store for a scene, like the ones that Store.parse reads.
  function toClip(name, fps, scene) {
    const hasLeds = scene.leds.length > 0;
    return {
      name, kind: 0, frames: scene.records.length, fps, imagesPerFrame: 0, sceneBlobs: scene.blobs.length, reserved: hasLeds ? 1 : 0,
      images: [scene.palette, ...scene.records, ...scene.blobs, ...(hasLeds ? [scene.leds] : [])],
    };
  }

  const api = { rle, dirtyRectangles, mergeTouching, splitForeground, buildPalette, makeQuantizer, sceneEncoder, encodeIndexed, ledFollower, ledTrack, toClip,
    WIDTH, HEIGHT, TUBE_COUNT, TUBE_PITCH, CANVAS_WIDTH };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
