// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Notices of a newer firmware. The page asks the site of the project for versions.json, which the release
// publishes there, and compares it with the version of the clock. The clock itself never goes to the internet.
//
// versions.json: {"latest": "0.2.0", "date": "2026-11-20", "notes": "...",
//                 "firmware": {"full": {"url": "https://.../dekatron-0.2.0-full.ota.bin", "sha256": "<64 hex>"}}}
const DekatronUpdate = (() => {
  const SITE = "https://yayitazale.github.io/dekatron/";
  const CHECK_EVERY_MS = 2 * 60 * 60 * 1000; // as eink-hub does: at most every two hours, and only when the page is open

  function parts(version) {
    const match = /^v?(\d+)\.(\d+)\.(\d+)$/.exec(String(version).trim());
    return match ? match.slice(1).map(Number) : null;
  }

  // -1, 0 or 1; null when either is not a version such as 1.2.3.
  function compare(a, b) {
    const x = parts(a);
    const y = parts(b);
    if (!x || !y) return null;
    for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
  }

  // The newer release for this clock, or null: nothing newer, a clock that does not say its version, or a file
  // that is not what it should be. A download must be https and come with its checksum.
  function pick(versions, current, profile = "full") {
    if (!versions || typeof versions !== "object" || compare(versions.latest, current) !== 1) return null;
    const file = versions.firmware && versions.firmware[profile];
    if (!file || typeof file.url !== "string" || !/^https:\/\//.test(file.url) || !/^[0-9a-f]{64}$/.test(file.sha256 || "")) return null;
    return {
      version: String(versions.latest).replace(/^v/, ""),
      date: typeof versions.date === "string" ? versions.date : "",
      notes: typeof versions.notes === "string" ? versions.notes.slice(0, 4000) : "",
      url: file.url,
      sha256: file.sha256,
    };
  }

  // SHA-256 in plain JavaScript, because crypto.subtle is only there on secure pages and the clock is http.
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  ]);

  function sha256(bytes) {
    const length = bytes.length;
    const padded = new Uint8Array(((length + 9 + 63) >> 6) << 6);
    padded.set(bytes);
    padded[length] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.length - 8, Math.floor(length / 0x20000000), false);
    view.setUint32(padded.length - 4, (length << 3) >>> 0, false);
    const state = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    const w = new Uint32Array(64);
    const rotr = (x, n) => (x >>> n) | (x << (32 - n));
    for (let at = 0; at < padded.length; at += 64) {
      for (let i = 0; i < 16; i++) w[i] = view.getUint32(at + i * 4, false);
      for (let i = 16; i < 64; i++) {
        const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
        const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
      }
      let [a, b, c, d, e, f, g, h] = state;
      for (let i = 0; i < 64; i++) {
        const t1 = (h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i]) | 0;
        const t2 = ((rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) | 0;
        [h, g, f, e, d, c, b, a] = [g, f, e, (d + t1) | 0, c, b, a, (t1 + t2) | 0];
      }
      state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    return Array.from(state, (word) => word.toString(16).padStart(8, "0")).join("");
  }

  // Whether the page should ask again: never asked, or asked more than two hours ago.
  const due = (lastCheckMs, nowMs = Date.now()) => !lastCheckMs || nowMs - lastCheckMs >= CHECK_EVERY_MS || nowMs < lastCheckMs;

  const api = { SITE, CHECK_EVERY_MS, compare, pick, sha256, due };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
