// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The simulator of the six tubes and the editor of their lights. It plays the frames of each animation of a theme
// on six tubes, with the six LEDs under them, and lets the user add flashes of light of their own at the moments
// they want. The lights that come out of it go into the theme file.
const DekatronPreview = (() => {
  const L = DekatronLights;
  const WIDTH = 685; // the strip of 1370 x 240 at half the size
  const HEIGHT = 120;
  const TUBE = 67.5;
  const PITCH = 123.5;
  const CANVAS_HEIGHT = 176;
  const BEZEL = "#0b0b0d";
  const hex = (color) => `#${color.map((value) => value.toString(16).padStart(2, "0")).join("")}`;
  const rgb = (value) => [1, 3, 5].map((at) => parseInt(value.slice(at, at + 2), 16));
  const tubeName = (tube) => (tube === "all" ? "All tubes" : `Tube ${tube + 1}${tube === 0 ? " (left)" : tube === 5 ? " (right)" : ""}`);

  function build({ h }) {
    const roles = []; // {role, label, fps, total, frames, auto, flashes, replace, final}
    let current = 0;
    let index = 0;
    let playing = false;
    let startedAt = 0;
    let startIndex = 0;

    const sim = h("canvas", { width: WIDTH, height: CANVAS_HEIGHT, class: "sim", "aria-label": "The six tubes and their lights" });
    const context = sim.getContext("2d");
    const offscreen = Object.assign(document.createElement("canvas"), { width: WIDTH, height: HEIGHT });
    const timeline = h("canvas", { width: WIDTH, height: 66, class: "timeline", "aria-label": "The lights over time" });
    const tabs = h("div", { class: "row" });
    const play = h("button", { class: "btn", onclick: () => (playing ? pause() : start()) }, "Pause");
    const scrub = h("input", { type: "range", min: 0, max: 0, value: 0, "aria-label": "Position", class: "grow" });
    const clock = h("span", { class: "muted" }, "0.0 s");
    const list = h("div", {});
    const mode = h("select", { class: "grow", "aria-label": "How the flashes mix with the picture" },
      [h("option", { value: "add" }, "Flashes are added to the lights of the picture"), h("option", { value: "replace" }, "Flashes replace the lights of the picture")]);
    const tube = h("select", { "aria-label": "Tube" }, [h("option", { value: "all" }, tubeName("all")), ...[0, 1, 2, 3, 4, 5].map((n) => h("option", { value: String(n) }, tubeName(n)))]);
    const from = h("input", { type: "number", min: 0, step: 0.1, value: 0, "aria-label": "Starts at, seconds" });
    const length = h("input", { type: "number", min: 0.1, step: 0.1, value: 0.5, "aria-label": "Lasts, seconds" });
    const color = h("input", { type: "color", value: "#ffffff", "aria-label": "Color" });
    const strength = h("input", { type: "range", min: 10, max: 100, value: 100, "aria-label": "Strength", class: "grow" });
    const fade = h("input", { type: "number", min: 0, step: 0.1, value: 0.2, "aria-label": "Fade, seconds" });
    const hint = h("p", { class: "muted" });

    const now = () => roles[current];
    const time = () => (now() ? index / now().fps : 0);

    function drawSim() {
      const r = now();
      if (!r) return;
      const frame = r.frames[Math.min(index, r.total - 1)];
      offscreen.getContext("2d").putImageData(new ImageData(frame.data, frame.width, frame.height), 0, 0);
      context.fillStyle = BEZEL;
      context.fillRect(0, 0, WIDTH, CANVAS_HEIGHT);
      context.drawImage(offscreen, 0, 0);
      // What lies between the tubes is never shown, and the tubes have a thin rim.
      context.fillStyle = BEZEL;
      for (let k = 0; k < 5; k++) context.fillRect(k * PITCH + TUBE, 0, PITCH - TUBE, HEIGHT);
      context.strokeStyle = "#2b2b31";
      context.lineWidth = 1;
      for (let position = 0; position < 6; position++) context.strokeRect(position * PITCH + 0.5, 0.5, TUBE - 1, HEIGHT - 1);
      for (let position = 0; position < 6; position++) {
        const [red, green, blue] = L.color(r.final, Math.min(index, r.total - 1), L.ledOfPosition(position));
        const x = position * PITCH + TUBE / 2;
        const y = 148;
        const glow = context.createRadialGradient(x, y, 2, x, y, 30);
        glow.addColorStop(0, `rgba(${red},${green},${blue},.7)`);
        glow.addColorStop(1, `rgba(${red},${green},${blue},0)`);
        context.fillStyle = glow;
        context.fillRect(x - 32, y - 32, 64, 64);
        context.beginPath();
        context.arc(x, y, 7, 0, Math.PI * 2);
        context.fillStyle = `rgb(${red},${green},${blue})`;
        context.fill();
        context.strokeStyle = "#2b2b31";
        context.stroke();
      }
    }

    function drawTimeline() {
      const r = now();
      const draw = timeline.getContext("2d");
      draw.fillStyle = BEZEL;
      draw.fillRect(0, 0, WIDTH, 66);
      if (!r) return;
      for (let position = 0; position < 6; position++) {
        for (let x = 0; x < WIDTH; x++) {
          const [red, green, blue] = L.color(r.final, Math.min(r.total - 1, Math.floor((x / WIDTH) * r.total)), L.ledOfPosition(position));
          draw.fillStyle = `rgb(${red},${green},${blue})`;
          draw.fillRect(x, position * 11, 1, 10);
        }
      }
      draw.fillStyle = "#fff";
      draw.fillRect(Math.floor((index / Math.max(1, r.total - 1)) * (WIDTH - 2)), 0, 2, 66);
    }

    function show() {
      const r = now();
      if (!r) return;
      scrub.max = r.total - 1;
      scrub.value = index;
      clock.textContent = `${time().toFixed(1)} s of ${(r.total / r.fps).toFixed(1)} s`;
      drawSim();
      drawTimeline();
    }

    function tick(stamp) {
      if (!playing) return;
      const r = now();
      index = (startIndex + Math.floor(((stamp - startedAt) / 1000) * r.fps)) % r.total;
      show();
      requestAnimationFrame(tick);
    }
    function start() {
      playing = true;
      play.textContent = "Pause";
      startedAt = performance.now();
      startIndex = index;
      requestAnimationFrame(tick);
    }
    function pause() {
      playing = false;
      play.textContent = "Play";
    }
    scrub.addEventListener("input", () => {
      pause();
      index = +scrub.value;
      from.value = time().toFixed(1);
      show();
    });

    // The lights as they will be in the file: those of the picture with the flashes on them.
    function remake(r) {
      r.final = L.merge(r.auto, r.total, r.fps, r.flashes, r.replace);
    }

    function renderList() {
      const r = now();
      list.replaceChildren(...(r.flashes.length ? r.flashes.map((flash, number) => h("div", { class: "flash" },
        h("span", { class: "dot", style: `background:${hex(flash.color)}` }),
        h("span", { class: "grow" }, `${tubeName(flash.tube)} · ${flash.start.toFixed(1)} s for ${flash.length.toFixed(1)} s · ${Math.round(flash.strength * 100)}%${flash.fade ? ` · fade ${flash.fade.toFixed(1)} s` : ""}`),
        h("button", { class: "btn", onclick: () => { r.flashes.splice(number, 1); remake(r); renderList(); show(); } }, "Remove")))
        : [h("p", { class: "muted" }, "No flashes yet. Move the slider to a moment, choose a tube and a color, and add one.")]));
    }

    function choose(number) {
      current = number;
      index = 0;
      mode.value = now().replace ? "replace" : "add";
      [...tabs.children].forEach((button, at) => button.classList.toggle("on", at === number));
      from.value = "0";
      renderList();
      show();
    }

    mode.addEventListener("change", () => { now().replace = mode.value === "replace"; remake(now()); show(); });
    const add = h("button", { class: "btn primary", onclick: () => {
      const r = now();
      if (!(+length.value > 0)) return;
      r.flashes.push({
        tube: tube.value === "all" ? "all" : +tube.value, start: Math.max(0, +from.value || 0), length: +length.value,
        color: rgb(color.value), strength: +strength.value / 100, fade: Math.max(0, +fade.value || 0),
      });
      remake(r);
      renderList();
      show();
    } }, "Add flash");

    const element = h("section", { class: "card", hidden: true },
      h("h2", {}, "Preview and lights"),
      tabs,
      h("div", { style: "margin-top:14px" }, sim),
      h("div", { class: "row" }, play, scrub, clock),
      h("p", { class: "muted" }, "Lights over time, one row for each tube, from the left"),
      timeline,
      h("h3", { style: "margin-top:20px" }, "Flashes of light"),
      hint,
      h("div", { class: "row" }, mode),
      h("div", { class: "row" }, tube, h("span", { class: "muted" }, "from"), from, h("span", { class: "muted" }, "s, for"), length, h("span", { class: "muted" }, "s")),
      h("div", { class: "row" }, color, h("span", { class: "muted" }, "Strength"), strength, h("span", { class: "muted" }, "Fade"), fade, h("span", { class: "muted" }, "s")),
      h("div", { class: "row" }, add),
      list);

    return {
      element,
      // data: [{role, label, fps, total, frames, auto}] from DekatronCreator.previewRole; follows is false when
      // the lights are only those of the flashes, which then start from dark.
      load(data, follows) {
        pause();
        roles.length = 0;
        for (const item of data) {
          const r = { ...item, auto: follows ? item.auto : new Uint8Array(item.total * L.FRAME_BYTES), flashes: [], replace: false };
          remake(r);
          roles.push(r);
        }
        tabs.replaceChildren(...roles.map((r, number) => h("button", { class: "btn mode", onclick: () => choose(number) }, r.label)));
        hint.textContent = follows
          ? "The lights follow the picture. Add flashes over them, for a spark at a moment, or switch to replacing them."
          : "The lights start dark. Add the flashes that you want, and they are all the lights the theme has.";
        element.hidden = false;
        choose(0);
        start();
      },
      leds: () => Object.fromEntries(roles.map((r) => [r.role, r.final])),
      stop: pause,
      clear() { pause(); roles.length = 0; element.hidden = true; },
    };
  }

  return { build };
})();
