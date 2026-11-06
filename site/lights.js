// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The lights of a theme: one color for each of the six LEDs on every frame, 18 bytes a frame. They come from the
// picture (see DekatronScene.ledFollower) and the creator lets the user add flashes of their own over them.
// The tube at the left of the clock is tube 5, and the LED of a tube has the number of the tube, so the
// positions that the user sees, 0 to 5 from the left, are turned into LED numbers here.
const DekatronLights = (() => {
  const LEDS = 6;
  const FRAME_BYTES = LEDS * 3;
  const ledOfPosition = (position) => LEDS - 1 - position;
  const clamp = (value) => Math.max(0, Math.min(255, Math.round(value)));

  // The same track, brighter or dimmer.
  function scale(track, gain) {
    return Uint8Array.from(track, (value) => clamp(value * gain));
  }

  // A flash is {tube: "all" or 0..5 from the left, start and length in seconds, color: [r, g, b], strength: 0..1,
  // fade: seconds of rise and fall at its ends}. It is added to the lights that the picture gave, or, with
  // `replace`, it takes their place in proportion to its strength. Gives a new track.
  function merge(track, frames, fps, flashes, replace = false) {
    const out = Uint8Array.from(track);
    for (const flash of flashes) {
      const first = Math.max(0, Math.round(flash.start * fps));
      const last = Math.min(frames, Math.round((flash.start + flash.length) * fps));
      if (last <= first) continue;
      const rise = Math.max(0, flash.fade || 0) * fps;
      const leds = flash.tube === "all" ? [...Array(LEDS).keys()] : [ledOfPosition(flash.tube)];
      for (let frame = first; frame < last; frame++) {
        let envelope = 1;
        if (rise > 0) envelope = Math.min(1, (frame - first + 1) / rise, (last - frame) / rise);
        const weight = Math.max(0, Math.min(1, flash.strength * envelope));
        for (const led of leds) {
          for (let channel = 0; channel < 3; channel++) {
            const at = frame * FRAME_BYTES + led * 3 + channel;
            const base = out[at];
            out[at] = clamp(replace ? base * (1 - weight) + flash.color[channel] * weight : base + flash.color[channel] * weight);
          }
        }
      }
    }
    return out;
  }

  // The color of one LED on one frame, as [r, g, b].
  const color = (track, frame, led) => [track[frame * FRAME_BYTES + led * 3], track[frame * FRAME_BYTES + led * 3 + 1], track[frame * FRAME_BYTES + led * 3 + 2]];

  const api = { LEDS, FRAME_BYTES, ledOfPosition, scale, merge, color };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
