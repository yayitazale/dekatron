// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The store of the clock, as tools/make_clips.py writes it and components/elekstube_ips/store.cpp reads it.
// Everything is little endian: a header, one entry per face or clip, the tables of images, and the images.
const DekatronStore = (() => {
  const MAGIC = [0x45, 0x4b, 0x53, 0x54]; // EKST
  const VERSION = 2;
  const HEADER_BYTES = 20;
  const ENTRY_BYTES = 32;
  const NAME_BYTES = 16;
  const TABLE_ENTRY_BYTES = 8;
  const KIND_CLIP = 0;
  const KIND_FACE = 1;
  const KIND_THEME = 2;
  const MAX_FACES = 4; // besides the one built into the firmware
  const FACE_IMAGES = 10;
  const FACE_INFO_IMAGES = 11; // a face may have an eleventh image: its information as JSON
  const SCENE = 0; // images per frame of a scene

  const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      table[n] = c >>> 0;
    }
    return table;
  })();

  function crc32(bytes, start = 0, end = bytes.length) {
    let crc = 0xffffffff;
    for (let i = start; i < end; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
  }

  function tableLength(entry) {
    return entry.imagesPerFrame === SCENE ? 1 + entry.frames + entry.sceneBlobs + (entry.reserved & 1) : entry.frames * entry.imagesPerFrame;
  }

  function fail(rule, message) {
    const error = new Error(message);
    error.rule = rule;
    return error;
  }

  // Reads a store. Throws an Error that says what is wrong, after checking it like the clock does.
  // The `rule` of the error is the same word that tools/check_bin.py uses.
  function parse(buffer) {
    const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    if (bytes.length < HEADER_BYTES) throw fail("not-a-store", "too short");
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (!MAGIC.every((value, i) => bytes[i] === value)) throw fail("not-a-store", "not a store");
    if (view.getUint32(4, true) !== VERSION) throw fail("version", "another version of the format");
    const total = view.getUint32(8, true);
    const count = view.getUint32(16, true);
    if (total !== bytes.length) throw fail("size-mismatch", `the header says ${total} bytes and the file has ${bytes.length}`);
    if (crc32(bytes, HEADER_BYTES, total) !== view.getUint32(12, true)) throw fail("bad-crc", "damaged: the checksum does not match");
    if (count < 1 || count > 255 || HEADER_BYTES + count * ENTRY_BYTES > total) throw fail("malformed", "malformed");

    const entries = [];
    for (let i = 0; i < count; i++) {
      const at = HEADER_BYTES + i * ENTRY_BYTES;
      let end = 0;
      while (end < NAME_BYTES && bytes[at + end] !== 0) end++;
      const entry = {
        name: new TextDecoder().decode(bytes.subarray(at, at + end)),
        frames: view.getUint32(at + 20, true),
        fps: view.getUint16(at + 24, true),
        imagesPerFrame: bytes[at + 26],
        kind: bytes[at + 27],
        sceneBlobs: view.getUint16(at + 28, true),
        reserved: view.getUint16(at + 30, true), // bit 0: a scene has a track of lights, which is its last image
        images: [],
      };
      const table = view.getUint32(at + 16, true);
      const length = tableLength(entry);
      if (table + length * TABLE_ENTRY_BYTES > total) throw fail("malformed", `${entry.name}: malformed`);
      for (let j = 0; j < length; j++) {
        const offset = view.getUint32(table + j * TABLE_ENTRY_BYTES, true);
        const size = view.getUint32(table + j * TABLE_ENTRY_BYTES + 4, true);
        if (offset + size > total) throw fail("malformed", `${entry.name}: malformed`);
        entry.images.push(bytes.subarray(offset, offset + size));
      }
      entries.push(entry);
    }
    return { entries, size: total };
  }

  function sameBytes(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }

  // Writes a store from entries like the ones parse() gives. Identical images are kept once.
  // `capacity` is the most it may take.
  function build(entries, capacity = Infinity) {
    if (!entries.length) throw new Error("nothing to store");
    if (entries.length > 255) throw new Error("at most 255 faces and clips");
    if (entries.filter((entry) => entry.kind === KIND_FACE).length > MAX_FACES) throw new Error(`a store holds at most ${MAX_FACES} faces`);
    if (entries.filter((entry) => entry.kind === KIND_THEME).length > 1) throw new Error("a store holds one theme");
    for (const [index, entry] of entries.entries()) {
      const encoded = new TextEncoder().encode(entry.name);
      if (encoded.length < 1 || encoded.length >= NAME_BYTES) throw new Error(`invalid name: ${entry.name}`);
      if (entries.some((other, j) => j !== index && other.kind === entry.kind && other.name === entry.name)) {
        throw new Error(`the name ${entry.name} is used twice`);
      }
      if (entry.images.length !== tableLength(entry)) throw new Error(`${entry.name}: wrong number of images`);
    }

    let position = HEADER_BYTES + ENTRY_BYTES * entries.length;
    const tables = [];
    for (const entry of entries) {
      tables.push(position);
      position += tableLength(entry) * TABLE_ENTRY_BYTES;
    }
    const payloadStart = position;

    // Images with the same bytes share one place, found through a cheap hash that is then checked.
    const places = new Map();
    const placed = [];
    let payloadSize = 0;
    const locate = (image) => {
      let hash = 0x811c9dc5;
      for (let i = 0; i < image.length; i++) hash = Math.imul(hash ^ image[i], 0x01000193) >>> 0;
      const key = `${hash}:${image.length}`;
      const candidates = places.get(key) || [];
      for (const candidate of candidates) if (sameBytes(candidate.image, image)) return candidate.offset;
      const offset = payloadStart + payloadSize;
      payloadSize += image.length;
      candidates.push({ image, offset });
      places.set(key, candidates);
      placed.push({ image, offset });
      return offset;
    };
    const offsets = entries.map((entry) => entry.images.map(locate));

    const total = payloadStart + payloadSize;
    if (total > capacity) throw new Error(`it takes ${total} bytes and the clock has room for ${capacity}`);
    const out = new Uint8Array(total);
    const view = new DataView(out.buffer);
    out.set(MAGIC, 0);
    view.setUint32(4, VERSION, true);
    view.setUint32(8, total, true);
    view.setUint32(16, entries.length, true);
    entries.forEach((entry, i) => {
      const at = HEADER_BYTES + i * ENTRY_BYTES;
      out.set(new TextEncoder().encode(entry.name), at);
      view.setUint32(at + 16, tables[i], true);
      view.setUint32(at + 20, entry.frames, true);
      view.setUint16(at + 24, entry.fps, true);
      out[at + 26] = entry.imagesPerFrame;
      out[at + 27] = entry.kind;
      view.setUint16(at + 28, entry.sceneBlobs, true);
      view.setUint16(at + 30, entry.reserved || 0, true);
      entry.images.forEach((image, j) => {
        view.setUint32(tables[i] + j * TABLE_ENTRY_BYTES, offsets[i][j], true);
        view.setUint32(tables[i] + j * TABLE_ENTRY_BYTES + 4, image.length, true);
      });
    });
    for (const { image, offset } of placed) out.set(image, offset);
    view.setUint32(12, crc32(out, HEADER_BYTES, total), true);
    return out;
  }

  // A clock face is ten images, one per digit, stored like a clip with one frame. With `info`, such as the
  // author and license, it has an eleventh image that holds them as JSON.
  function face(name, digitImages, info) {
    if (digitImages.length !== FACE_IMAGES) throw new Error("a face needs ten images");
    const images = [...digitImages];
    if (info && Object.keys(info).length) {
      const bytes = new TextEncoder().encode(JSON.stringify(info));
      if (bytes.length > 512) throw new Error("the data of a face is too long");
      images.push(bytes);
    }
    return { name, kind: KIND_FACE, frames: 1, fps: 1, imagesPerFrame: images.length, sceneBlobs: 0, images };
  }

  function faceInfo(entry) {
    if (entry.imagesPerFrame !== FACE_INFO_IMAGES) return {};
    return themeInfo({ images: [entry.images[FACE_IMAGES]] });
  }

  // A theme is its name and a little data, such as its author and license, kept as JSON in its one image.
  function theme(name, info) {
    const bytes = new TextEncoder().encode(JSON.stringify(info));
    if (bytes.length > 512) throw new Error("the data of a theme is too long");
    return { name, kind: KIND_THEME, frames: 1, fps: 1, imagesPerFrame: 1, sceneBlobs: 0, images: [bytes] };
  }

  function themeInfo(entry) {
    try {
      return JSON.parse(new TextDecoder().decode(entry.images[0]));
    } catch (error) {
      return {};
    }
  }


  // ---- Checking a file for the clock, with the rules of the clock. The same rules, in the same order and
  // with the same words for each one, as tools/check_bin.py: a test compares them.
  const TUBE_COUNT = 6;
  const TUBE_WIDTH = 135;
  const TUBE_HEIGHT = 240;
  const LED_FRAME_BYTES = 18;
  const THEME_ROLES = ["boot", "connecting", "online", "countdown_end"];
  const THEME_BYTES = 800 * 1024; // what a theme may take of the store
  const REGION_BYTES = 0x120000; // what the store of the clock holds
  const APP_BYTES = 0x160000; // the partition of the app
  const GALLERY_LICENSE = "CC-BY-SA-4.0";
  const MAX_REPORTED = 3;
  const SOF_MARKERS = [0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf];

  // The size and kind of a JPEG, or a string that says why the clock cannot read it.
  function jpegInfo(data) {
    if (data[0] !== 0xff || data[1] !== 0xd8) return "not a JPEG: it does not start with the JPEG marker";
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let i = 2;
    while (i + 4 <= data.length) {
      if (data[i] !== 0xff) return "damaged JPEG: a marker is missing";
      const marker = data[i + 1];
      if (marker === 0xff) {
        i += 1;
        continue;
      }
      if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd8)) {
        i += 2;
        continue;
      }
      const length = view.getUint16(i + 2, false);
      if (SOF_MARKERS.includes(marker)) {
        if (i + 10 > data.length) return "damaged JPEG: it ends inside its header";
        const components = data[i + 9];
        const sampling = [];
        for (let n = 0; n < components; n++) if (i + 11 + 3 * n < data.length) sampling.push(data[i + 11 + 3 * n]);
        return {
          baseline: marker === 0xc0,
          height: view.getUint16(i + 5, false),
          width: view.getUint16(i + 7, false),
          subsampling420: components === 3 && sampling[0] === 0x22 && sampling[1] === 0x11 && sampling[2] === 0x11,
        };
      }
      if (marker === 0xda) return "damaged JPEG: the picture starts before its size";
      i += 2 + length;
    }
    return "damaged JPEG: it has no size";
  }

  function classify(entries) {
    const faces = entries.filter((e) => e.kind === KIND_FACE);
    const themes = entries.filter((e) => e.kind === KIND_THEME);
    const clips = entries.filter((e) => e.kind === KIND_CLIP);
    if (faces.length === 1 && !themes.length && !clips.length) return "face";
    if (themes.length === 1 && !faces.length) return "theme";
    return "backup";
  }

  // Checks the bytes of a file for the clock. Gives {type, name, info, problems, warnings, ok}, where type is
  // "theme", "face", "backup" or "firmware", and each problem or warning is {rule, message}.
  function checkBin(buffer, { gallery = false } = {}) {
    const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    const report = { type: "unknown", name: "", info: {}, problems: [], warnings: [] };
    const problem = (rule, message) => report.problems.push({ rule, message });
    const warn = (rule, message) => report.warnings.push({ rule, message });
    const done = () => {
      report.ok = report.problems.length === 0;
      return report;
    };

    const isStore = MAGIC.every((value, i) => bytes[i] === value);
    if (!isStore && bytes[0] === 0xe9) {
      report.type = "firmware";
      if (bytes.length < 0x10000) problem("firmware-size", "The file is too small to be a firmware.");
      else if (bytes.length > APP_BYTES) problem("firmware-size", `The firmware takes ${bytes.length} bytes and the app partition holds ${APP_BYTES}.`);
      if (bytes.length >= 14 && (bytes[12] | (bytes[13] << 8)) !== 0) problem("firmware-chip", "This firmware is for another chip, not the ESP32 of the clock.");
      return done();
    }

    let parsed;
    try {
      parsed = parse(bytes);
    } catch (error) {
      problem(error.rule || "malformed", error.message);
      return done();
    }
    const entries = parsed.entries;
    const faces = entries.filter((e) => e.kind === KIND_FACE);
    const themes = entries.filter((e) => e.kind === KIND_THEME);
    const clips = entries.filter((e) => e.kind === KIND_CLIP);
    report.type = classify(entries);

    if (faces.length > MAX_FACES) problem("too-many-faces", `It has ${faces.length} faces and the store holds ${MAX_FACES} besides the one in the firmware.`);
    if (themes.length > 1) problem("several-themes", "It has more than one theme and the store holds one.");
    for (const kind of [KIND_FACE, KIND_CLIP]) {
      const names = entries.filter((e) => e.kind === kind).map((e) => e.name);
      if (new Set(names).size !== names.length) problem("duplicate-name", "Two faces or two clips have the same name. Names must be unique.");
    }
    for (const entry of entries) {
      const length = new TextEncoder().encode(entry.name).length;
      if (length < 1 || length >= NAME_BYTES) problem("bad-name", `${JSON.stringify(entry.name)}: a name takes 1 to ${NAME_BYTES - 1} bytes.`);
    }

    const checkJpegs = (entry, images) => {
      let reported = 0;
      for (const [number, image] of images.entries()) {
        const info = jpegInfo(image);
        const where = `${entry.name}, image ${number}`;
        let rule;
        let message;
        if (typeof info === "string") [rule, message] = ["jpeg-invalid", `${where}: ${info}.`];
        else if (!info.baseline) [rule, message] = ["jpeg-not-baseline", `${where}: the clock reads only baseline JPEG, and this one is progressive or of another kind. Save it again as a standard (baseline) JPEG.`];
        else if (info.width !== TUBE_WIDTH || info.height !== TUBE_HEIGHT) [rule, message] = ["jpeg-size", `${where}: it is ${info.width}x${info.height} and the tubes are ${TUBE_WIDTH}x${TUBE_HEIGHT}.`];
        else {
          if (!info.subsampling420) warn("jpeg-subsampling", `${where}: it is not 4:2:0, which is what the tools write and the clock has been tried with.`);
          continue;
        }
        problem(rule, message);
        reported++;
        if (reported >= MAX_REPORTED) return;
      }
    };

    const readInfo = (entry, what) => {
      try {
        const info = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(entry.images[entry.images.length - 1]));
        if (info === null || typeof info !== "object" || Array.isArray(info)) throw new Error("not an object");
        return info;
      } catch (error) {
        warn("info-invalid", `${what}: its information is not a JSON object.`);
        return {};
      }
    };

    const checkScene = (clip) => {
      const name = clip.name;
      const images = clip.images;
      if (images[0].length !== 512) return problem("scene-invalid", `${name}: the palette must take 512 bytes (256 colors of 16 bits) and takes ${images[0].length}.`);
      for (let frame = 0; frame < clip.frames; frame++) {
        const record = images[1 + frame];
        if (record.length < 2) return problem("scene-invalid", `${name}: frame ${frame} has no record.`);
        const view = new DataView(record.buffer, record.byteOffset, record.byteLength);
        const count = view.getUint16(0, true);
        if (record.length !== 2 + count * 8) return problem("scene-invalid", `${name}: frame ${frame} says ${count} patches but its record has another size.`);
        for (let n = 0; n < count; n++) {
          const at = 2 + n * 8;
          const [tube, x, y, width, height] = [record[at], record[at + 1], record[at + 2], record[at + 3], record[at + 4]];
          const blob = view.getUint16(at + 6, true);
          if (tube >= TUBE_COUNT || width === 0 || height === 0 || x + width > TUBE_WIDTH || y + height > TUBE_HEIGHT || blob >= clip.sceneBlobs) {
            return problem("scene-invalid", `${name}: a patch of frame ${frame} is outside its tube or points to a missing blob.`);
          }
        }
      }
      if ((clip.reserved & 1) && images[1 + clip.frames + clip.sceneBlobs].length !== clip.frames * LED_FRAME_BYTES) {
        problem("scene-invalid", `${name}: the lights need ${LED_FRAME_BYTES} bytes for each frame (${clip.frames * LED_FRAME_BYTES}).`);
      }
    };

    for (const face of faces) {
      if (face.frames !== 1 || (face.imagesPerFrame !== FACE_IMAGES && face.imagesPerFrame !== FACE_INFO_IMAGES)) {
        problem("face-images", `${face.name}: a face is ten images, one per digit, and may have an eleventh with its information.`);
        continue;
      }
      checkJpegs(face, face.images.slice(0, FACE_IMAGES));
      const info = face.imagesPerFrame === FACE_INFO_IMAGES ? readInfo(face, face.name) : {};
      if (faces.length === 1 && report.type === "face") [report.name, report.info] = [face.name, info];
    }

    for (const clip of clips) {
      if (clip.fps === 0 || clip.fps > 60 || clip.frames === 0) problem("clip-shape", `${clip.name}: it needs 1 to 60 frames per second and at least one frame.`);
      else if (clip.imagesPerFrame === SCENE) checkScene(clip);
      else if (clip.imagesPerFrame === 1 || clip.imagesPerFrame === TUBE_COUNT) checkJpegs(clip, clip.images);
      else problem("clip-shape", `${clip.name}: a frame is one image for every tube or one for each of the six, and this one has ${clip.imagesPerFrame}.`);
    }

    if (report.type === "theme" && themes.length === 1) {
      const theme = themes[0];
      report.name = theme.name;
      report.info = readInfo(theme, theme.name);
      const names = clips.map((clip) => clip.name);
      for (const role of THEME_ROLES) {
        if (!names.includes(role)) problem("theme-missing-clip", `The theme has no animation named ${role}. A theme has exactly boot, connecting, online and countdown_end.`);
      }
      for (const extra of [...new Set(names.filter((n) => !THEME_ROLES.includes(n)))].sort()) {
        problem("theme-extra-clip", `The animation ${extra} is not part of a theme, which has only boot, connecting, online and countdown_end.`);
      }
      const unique = new Map();
      for (const entry of [theme, ...clips]) for (const image of entry.images) unique.set(`${image.byteOffset}:${image.length}`, image.length);
      const used = [...unique.values()].reduce((sum, size) => sum + size, 0);
      if (used > THEME_BYTES) problem("theme-size", `The theme takes ${used} bytes and the limit is ${THEME_BYTES}. Use fewer frames, a lower frame rate or fewer colors.`);
    }
    if (bytes.length > REGION_BYTES) problem("store-size", `The file takes ${bytes.length} bytes and the store of the clock holds ${REGION_BYTES}.`);

    if (report.type === "theme" || report.type === "face") {
      if (!report.info.author) warn("info-author", "No author in its information.");
      if (!report.info.license) warn("info-license", "No license in its information.");
      if (gallery) {
        if (!report.info.author) problem("gallery-author", "The gallery needs the author in the information.");
        if (report.info.license !== GALLERY_LICENSE) problem("gallery-license", `The gallery takes only "license": "${GALLERY_LICENSE}".`);
      }
    }
    return done();
  }

  // What a file is, by what is inside it: "theme", "face", "backup" or "firmware". Throws if it is none.
  function kindOf(buffer) {
    const report = checkBin(buffer);
    if (report.type === "unknown") throw new Error(report.problems[0].message);
    return report.type;
  }

  const api = { parse, build, face, faceInfo, theme, themeInfo, checkBin, kindOf, jpegInfo, crc32, KIND_CLIP, KIND_FACE, KIND_THEME, MAX_FACES, FACE_IMAGES, FACE_INFO_IMAGES, SCENE, NAME_BYTES };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
