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

  // Reads a store. Throws an Error that says what is wrong, after checking it like the clock does.
  function parse(buffer) {
    const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    if (bytes.length < HEADER_BYTES) throw new Error("too short");
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (!MAGIC.every((value, i) => bytes[i] === value)) throw new Error("not a store");
    if (view.getUint32(4, true) !== VERSION) throw new Error("another version of the format");
    const total = view.getUint32(8, true);
    const count = view.getUint32(16, true);
    if (total !== bytes.length) throw new Error(`the header says ${total} bytes and the file has ${bytes.length}`);
    if (crc32(bytes, HEADER_BYTES, total) !== view.getUint32(12, true)) throw new Error("damaged: the checksum does not match");
    if (count < 1 || count > 255 || HEADER_BYTES + count * ENTRY_BYTES > total) throw new Error("malformed");

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
      if (table + length * TABLE_ENTRY_BYTES > total) throw new Error(`${entry.name}: malformed`);
      for (let j = 0; j < length; j++) {
        const offset = view.getUint32(table + j * TABLE_ENTRY_BYTES, true);
        const size = view.getUint32(table + j * TABLE_ENTRY_BYTES + 4, true);
        if (offset + size > total) throw new Error(`${entry.name}: malformed`);
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

  // A clock face is ten images, one per digit, stored like a clip with one frame.
  function face(name, digitImages) {
    if (digitImages.length !== FACE_IMAGES) throw new Error("a face needs ten images");
    return { name, kind: KIND_FACE, frames: 1, fps: 1, imagesPerFrame: FACE_IMAGES, sceneBlobs: 0, images: digitImages };
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

  const api = { parse, build, face, theme, themeInfo, crc32, KIND_CLIP, KIND_FACE, KIND_THEME, MAX_FACES, FACE_IMAGES, SCENE, NAME_BYTES };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
