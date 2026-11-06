// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A color picker of our own, in place of the box that each browser draws for <input type="color">, which is
// small and ugly on a computer and different on every phone. A round swatch opens a sheet with a square for
// saturation and brightness, a hue slider, a few common colors and the hex value. It works with a finger.
// picker() gives a button that behaves like the input: .value is "#rrggbb", and it fires "input" while the
// color moves and "change" when a choice is made, so the code that read the input needs no change.
const DekatronColor = (() => {
  const PRESETS = ["#ffb020", "#ff7a59", "#ff5fa8", "#8b7bff", "#3fb6ff", "#4fe0a0", "#ffffff", "#ff2d2d", "#ffe45c", "#000000"];
  const clamp = (value, low, high) => Math.min(high, Math.max(low, value));
  const isHex = (value) => /^#[0-9a-f]{6}$/i.test(value);

  // Hue 0 to 360, saturation and brightness 0 to 1.
  function hexToHsv(hex) {
    const [r, g, b] = [1, 3, 5].map((at) => parseInt(hex.slice(at, at + 2), 16) / 255);
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const delta = max - min;
    let hue = 0;
    if (delta) {
      if (max === r) hue = ((g - b) / delta) % 6;
      else if (max === g) hue = (b - r) / delta + 2;
      else hue = (r - g) / delta + 4;
      hue = (hue * 60 + 360) % 360;
    }
    return [hue, max ? delta / max : 0, max];
  }

  function hsvToHex(hue, saturation, value) {
    const chroma = value * saturation;
    const x = chroma * (1 - Math.abs(((hue / 60) % 2) - 1));
    const m = value - chroma;
    const [r, g, b] = [[chroma, x, 0], [x, chroma, 0], [0, chroma, x], [0, x, chroma], [x, 0, chroma], [chroma, 0, x]][Math.floor((hue % 360) / 60)];
    return `#${[r, g, b].map((channel) => Math.round((channel + m) * 255).toString(16).padStart(2, "0")).join("")}`;
  }

  let styled = false;
  function addStyle() {
    if (styled || typeof document === "undefined") return;
    styled = true;
    const style = document.createElement("style");
    style.textContent = `
.dkc-swatch { width: 34px; height: 34px; border-radius: 10px; border: 3px solid Canvas; box-shadow: 0 0 0 1px color-mix(in srgb, CanvasText 30%, transparent); padding: 0; flex: none; cursor: pointer; }
dialog.dkc { color: CanvasText; background: Canvas; border: 0; border-radius: 18px; padding: 16px; width: min(320px, 94vw); box-shadow: 0 16px 40px rgb(0 0 0 / .4); }
dialog.dkc::backdrop { background: rgb(0 0 0 / .5); }
.dkc-plane { position: relative; width: 100%; aspect-ratio: 4 / 3; border-radius: 12px; touch-action: none; cursor: crosshair; overflow: hidden; }
.dkc-plane canvas { position: absolute; inset: 0; width: 100%; height: 100%; display: block; }
.dkc-dot { position: absolute; width: 22px; height: 22px; margin: -11px 0 0 -11px; border-radius: 50%; border: 3px solid #fff; box-shadow: 0 0 0 1px rgb(0 0 0 / .5), inset 0 0 0 1px rgb(0 0 0 / .3); pointer-events: none; }
.dkc-hue { -webkit-appearance: none; appearance: none; width: 100%; height: 26px; margin: 14px 0 6px; background: transparent; touch-action: pan-y; }
.dkc-hue::-webkit-slider-runnable-track { height: 14px; border-radius: 7px; background: linear-gradient(90deg, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00); }
.dkc-hue::-moz-range-track { height: 14px; border-radius: 7px; background: linear-gradient(90deg, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00); }
.dkc-hue::-webkit-slider-thumb { -webkit-appearance: none; width: 26px; height: 26px; margin-top: -6px; border-radius: 50%; background: #fff; border: 3px solid #fff; box-shadow: 0 0 0 1px rgb(0 0 0 / .5); }
.dkc-hue::-moz-range-thumb { width: 20px; height: 20px; border-radius: 50%; background: #fff; border: 3px solid #fff; box-shadow: 0 0 0 1px rgb(0 0 0 / .5); }
.dkc-row { display: flex; align-items: center; gap: 10px; margin-top: 12px; }
.dkc-row.wrap { display: grid; grid-template-columns: repeat(5, 1fr); justify-items: center; gap: 12px; }
.dkc-now { width: 44px; height: 38px; border-radius: 10px; box-shadow: 0 0 0 1px color-mix(in srgb, CanvasText 30%, transparent); flex: none; }
.dkc-hex { flex: 1; min-width: 0; font: inherit; font-family: ui-monospace, Consolas, monospace; text-transform: lowercase; padding: 9px 12px; border-radius: 10px; border: 1px solid color-mix(in srgb, CanvasText 25%, transparent); background: transparent; color: inherit; }
.dkc-preset { width: 30px; height: 30px; border-radius: 50%; padding: 0; border: 2px solid Canvas; box-shadow: 0 0 0 1px color-mix(in srgb, CanvasText 30%, transparent); cursor: pointer; }
.dkc-actions { display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px; }
.dkc-actions button { font: inherit; padding: 8px 18px; border-radius: 20px; border: 1px solid color-mix(in srgb, CanvasText 25%, transparent); background: transparent; color: inherit; cursor: pointer; }
.dkc-actions button.main { background: var(--accent, var(--brand, var(--button, #ffb020))); border-color: transparent; color: #1b1b1f; font-weight: 650; }
@media (max-width: 560px) { dialog.dkc { width: 100%; max-width: none; margin: auto 0 0; border-radius: 20px 20px 0 0; padding: 18px 18px calc(18px + env(safe-area-inset-bottom)); } }
`;
    document.head.append(style);
  }

  function picker({ value = "#ffffff", label = "Color", presets = PRESETS } = {}) {
    addStyle();
    let current = isHex(value) ? value.toLowerCase() : "#ffffff";
    const button = document.createElement("button");
    button.type = "button";
    button.className = "dkc-swatch";
    button.setAttribute("aria-label", label);
    const paint = () => { button.style.background = current; };
    paint();
    Object.defineProperty(button, "value", {
      get: () => current,
      set: (next) => { if (isHex(next)) { current = next.toLowerCase(); paint(); } },
    });
    const fire = (type) => button.dispatchEvent(new Event(type, { bubbles: true }));

    button.addEventListener("click", () => {
      const original = current;
      let [hue, saturation, brightness] = hexToHsv(current);
      const dialog = document.createElement("dialog");
      dialog.className = "dkc";
      dialog.setAttribute("aria-label", label);
      const plane = document.createElement("div");
      plane.className = "dkc-plane";
      const surface = document.createElement("canvas");
      surface.width = 240;
      surface.height = 180;
      const dot = document.createElement("div");
      dot.className = "dkc-dot";
      plane.append(surface, dot);
      const slider = Object.assign(document.createElement("input"), { type: "range", min: 0, max: 360, step: 1, className: "dkc-hue" });
      slider.setAttribute("aria-label", "Hue");
      const now = document.createElement("div");
      now.className = "dkc-now";
      const hex = Object.assign(document.createElement("input"), { type: "text", maxLength: 7, className: "dkc-hex", spellcheck: false, autocapitalize: "off" });
      hex.setAttribute("aria-label", "Hex color");
      const swatches = document.createElement("div");
      swatches.className = "dkc-row wrap";
      const cancel = Object.assign(document.createElement("button"), { type: "button", textContent: "Cancel" });
      const done = Object.assign(document.createElement("button"), { type: "button", textContent: "Done", className: "main" });
      const actions = document.createElement("div");
      actions.className = "dkc-actions";
      actions.append(cancel, done);
      const row = document.createElement("div");
      row.className = "dkc-row";
      row.append(now, hex);
      dialog.append(plane, slider, row, swatches, actions);

      const drawPlane = () => {
        const draw = surface.getContext("2d");
        draw.fillStyle = hsvToHex(hue, 1, 1);
        draw.fillRect(0, 0, surface.width, surface.height);
        const white = draw.createLinearGradient(0, 0, surface.width, 0);
        white.addColorStop(0, "#fff");
        white.addColorStop(1, "rgba(255,255,255,0)");
        draw.fillStyle = white;
        draw.fillRect(0, 0, surface.width, surface.height);
        const black = draw.createLinearGradient(0, 0, 0, surface.height);
        black.addColorStop(0, "rgba(0,0,0,0)");
        black.addColorStop(1, "#000");
        draw.fillStyle = black;
        draw.fillRect(0, 0, surface.width, surface.height);
      };
      const show = (type) => {
        current = hsvToHex(hue, saturation, brightness);
        paint();
        now.style.background = current;
        if (document.activeElement !== hex) hex.value = current;
        dot.style.left = `${saturation * 100}%`;
        dot.style.top = `${(1 - brightness) * 100}%`;
        slider.value = Math.round(hue);
        if (type) fire(type);
      };
      const setFrom = (hexValue) => { [hue, saturation, brightness] = hexToHsv(hexValue); drawPlane(); show("input"); fire("change"); };

      const move = (event) => {
        const box = plane.getBoundingClientRect();
        saturation = clamp((event.clientX - box.left) / box.width, 0, 1);
        brightness = 1 - clamp((event.clientY - box.top) / box.height, 0, 1);
        show("input");
      };
      plane.addEventListener("pointerdown", (event) => { plane.setPointerCapture(event.pointerId); move(event); });
      plane.addEventListener("pointermove", (event) => { if (plane.hasPointerCapture(event.pointerId)) move(event); });
      plane.addEventListener("pointerup", () => fire("change"));
      slider.addEventListener("input", () => { hue = +slider.value; drawPlane(); show("input"); });
      slider.addEventListener("change", () => fire("change"));
      hex.addEventListener("input", () => { if (isHex(hex.value)) setFrom(hex.value); });
      for (const preset of presets) {
        const swatch = Object.assign(document.createElement("button"), { type: "button", className: "dkc-preset" });
        swatch.style.background = preset;
        swatch.setAttribute("aria-label", preset);
        swatch.addEventListener("click", () => setFrom(preset));
        swatches.append(swatch);
      }
      const close = () => { dialog.close(); dialog.remove(); };
      done.addEventListener("click", close);
      cancel.addEventListener("click", () => { close(); if (current !== original) { current = original; paint(); fire("input"); fire("change"); } });
      dialog.addEventListener("cancel", (event) => { event.preventDefault(); cancel.click(); });
      document.body.append(dialog);
      drawPlane();
      show(null);
      dialog.showModal();
    });
    return button;
  }

  const api = { picker, hexToHsv, hsvToHex, isHex, PRESETS };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
