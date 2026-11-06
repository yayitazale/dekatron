// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Makes a clock face in the browser: ten pictures of 135 x 240 pixels, one per digit, drawn from a font or taken
// from ten images of the user, with its author and license. The result is a .face.bin that the Files tab of the
// page of the clock takes. Nothing leaves the computer, except that a font of the list is fetched from
// the internet, which tells nobody what you make.
const DekatronFaces = (() => {
  const WIDTH = 135;
  const HEIGHT = 240;

  // Free fonts of Fontsource (licenses OFL or Apache), which serves each as a file under cdn.jsdelivr.net.
  const FONTS = [
    ["orbitron", "Orbitron"], ["share-tech-mono", "Share Tech Mono"], ["audiowide", "Audiowide"], ["bungee", "Bungee"],
    ["monoton", "Monoton"], ["major-mono-display", "Major Mono Display"], ["syncopate", "Syncopate"], ["bebas-neue", "Bebas Neue"],
    ["oswald", "Oswald"], ["roboto-mono", "Roboto Mono"], ["space-mono", "Space Mono"], ["playfair-display", "Playfair Display"],
    ["press-start-2p", "Press Start 2P"], ["vt323", "VT323"], ["black-ops-one", "Black Ops One"], ["rubik-mono-one", "Rubik Mono One"],
  ];

  const canvas = () => Object.assign(document.createElement("canvas"), { width: WIDTH, height: HEIGHT });
  const toBytes = async (source, quality) => {
    const blob = await new Promise((resolve) => source.toBlob(resolve, "image/jpeg", quality));
    if (!blob) throw new Error("That image cannot be used");
    return new Uint8Array(await blob.arrayBuffer());
  };

  // The clock reads only baseline JPEG, which is marked SOF0 where a progressive one has SOF2. A canvas gives
  // baseline in every browser, and this checks it all the same.
  function assertBaseline(bytes) {
    for (let i = 2; i + 1 < bytes.length; ) {
      if (bytes[i] !== 0xff) { i++; continue; }
      if (bytes[i + 1] === 0xc2) throw new Error("The clock reads only baseline JPEG");
      if (bytes[i + 1] === 0xc0) return;
      i += 2 + ((bytes[i + 2] << 8) | bytes[i + 3]);
    }
  }

  // A picture of the user, fitted to the tube on black.
  function imageToJpeg(file) {
    return new Promise((resolve, reject) => {
      const link = URL.createObjectURL(file);
      const image = new Image();
      image.onerror = () => { URL.revokeObjectURL(link); reject(new Error(`${file.name} is not an image`)); };
      image.onload = async () => {
        URL.revokeObjectURL(link);
        const target = canvas();
        const context = target.getContext("2d");
        context.fillStyle = "#000";
        context.fillRect(0, 0, WIDTH, HEIGHT);
        const scale = Math.min(WIDTH / image.naturalWidth, HEIGHT / image.naturalHeight);
        const width = Math.max(1, Math.round(image.naturalWidth * scale));
        const height = Math.max(1, Math.round(image.naturalHeight * scale));
        context.imageSmoothingQuality = "high";
        context.drawImage(image, (WIDTH - width) >> 1, (HEIGHT - height) >> 1, width, height);
        try {
          const bytes = await toBytes(target, 0.88);
          assertBaseline(bytes);
          resolve(bytes);
        } catch (error) {
          reject(error);
        }
      };
      image.src = link;
    });
  }

  // Draws the ten digits in a font, each on its own canvas, on one baseline so the digits do not jump.
  function renderDigits(family, { size, color, glow, background }) {
    const probe = canvas().getContext("2d");
    probe.font = `100px ${family}`;
    const zero = probe.measureText("0");
    const inkHeight = zero.actualBoundingBoxAscent + zero.actualBoundingBoxDescent || 100;
    let pixels = (100 * (HEIGHT * size)) / inkHeight;
    const widest = () => Math.max(...[...Array(10).keys()].map((digit) => {
      probe.font = `${pixels}px ${family}`;
      const ink = probe.measureText(String(digit));
      return ink.actualBoundingBoxLeft + ink.actualBoundingBoxRight;
    }));
    if (widest() > 123) pixels *= 123 / widest();
    probe.font = `${pixels}px ${family}`;
    const base = probe.measureText("0");
    const baseline = (HEIGHT + base.actualBoundingBoxAscent - base.actualBoundingBoxDescent) / 2;
    return [...Array(10).keys()].map((digit) => {
      const target = canvas();
      const context = target.getContext("2d");
      context.fillStyle = background;
      context.fillRect(0, 0, WIDTH, HEIGHT);
      context.font = `${pixels}px ${family}`;
      context.textBaseline = "alphabetic";
      context.textAlign = "left";
      const ink = context.measureText(String(digit));
      const x = WIDTH / 2 - (ink.actualBoundingBoxRight - ink.actualBoundingBoxLeft) / 2;
      context.fillStyle = color;
      context.shadowColor = color;
      for (const blur of glow ? [glow, glow / 2] : [0]) {
        context.shadowBlur = blur;
        context.fillText(String(digit), x, baseline);
      }
      return target;
    });
  }

  // The card to make a face. `ui` has what the page already has: h, toast, card and Store.
  function build({ h, toast, card, Store }) {
    const name = h("input", { type: "text", maxlength: 15, placeholder: "Name of the face", class: "grow", "aria-label": "Name" });
    const author = h("input", { type: "text", maxlength: 40, placeholder: "Your name (optional)", class: "grow", "aria-label": "Author" });
    const license = h("input", { type: "text", maxlength: 40, value: "CC-BY-SA-4.0", class: "grow", "aria-label": "License" });
    const about = h("input", { type: "text", maxlength: 160, placeholder: "A line about it (optional)", class: "grow", "aria-label": "Description" });
    const result = h("div", {});
    const body = h("div", { style: "margin-top:14px" });
    // What each source gives: a function that makes the ten JPEG byte strings, or null while it cannot.
    let source = null;

    async function create() {
      result.replaceChildren();
      const faceName = name.value.trim();
      if (!faceName) return toast("Give the face a name", true);
      if (!source) return toast("Choose a font or ten images first", true);
      try {
        const images = await source();
        const info = {};
        if (author.value.trim()) info.author = author.value.trim();
        if (license.value.trim()) info.license = license.value.trim();
        if (about.value.trim()) info.description = about.value.trim();
        const bytes = Store.build([Store.face(faceName, images, info)]);
        const report = Store.checkBin(bytes);
        if (!report.ok) throw new Error(report.problems[0].message);
        const link = h("a", { class: "btn primary", href: URL.createObjectURL(new Blob([bytes])), download: `${faceName}.face.bin` }, `Download ${faceName}.face.bin`);
        result.replaceChildren(
          h("p", {}, `Ready: ${Math.round(bytes.length / 1024)} KB.`),
          h("p", { class: "muted" }, "On the page of your clock, open Files and upload this file. It says what the file is before it changes anything, and it goes into a free slot or replaces a face you choose."),
          h("div", { class: "row" }, link));
      } catch (error) {
        toast(error.message, true);
      }
    }

    // ---- from ten images ----
    function imagesPanel() {
      const picked = new Map();
      const status = h("p", { class: "muted" });
      const input = h("input", { type: "file", accept: "image/*", multiple: true, hidden: true });
      const update = () => {
        const missing = [...Array(10).keys()].filter((digit) => !picked.has(digit));
        status.textContent = picked.size ? `${picked.size} of 10 digits${missing.length ? `, missing ${missing.join(", ")}` : ""}` : "";
        source = missing.length ? null : () => Promise.all([...Array(10).keys()].map((digit) => imageToJpeg(picked.get(digit))));
      };
      const take = (files) => {
        for (const file of files) {
          const match = file.name.replace(/\.[^.]*$/, "").match(/(?:^|\D)(\d)(?:\D|$)/);
          if (match) picked.set(+match[1], file);
        }
        update();
      };
      input.addEventListener("change", () => take(input.files));
      const panel = h("div", {},
        h("div", { class: "row" }, h("button", { class: "btn", onclick: () => input.click() }, "Choose ten images"), input), status,
        h("p", { class: "muted" }, "Name the files 0 to 9, such as 0.png or digit-7.jpg. Each is fitted to the 135 × 240 tube on black. You can also drop them here."));
      panel.addEventListener("dragover", (event) => event.preventDefault());
      panel.addEventListener("drop", (event) => { event.preventDefault(); take(event.dataTransfer.files); });
      source = null;
      return panel;
    }

    // ---- from a font ----
    function fontPanel() {
      const family = `dk-face-${Math.random().toString(36).slice(2, 8)}`;
      let loaded = false;
      const select = h("select", { class: "grow", "aria-label": "Font" }, FONTS.map(([id, label]) => h("option", { value: id }, label)));
      const file = h("input", { type: "file", accept: ".ttf,.otf,.woff,.woff2", hidden: true });
      const size = h("input", { type: "range", min: 40, max: 90, value: 62, "aria-label": "Size" });
      const glow = h("input", { type: "range", min: 0, max: 30, value: 12, "aria-label": "Glow" });
      const color = h("input", { type: "color", value: "#ffb020", "aria-label": "Digit color" });
      const back = h("input", { type: "color", value: "#000000", "aria-label": "Background color" });
      const strip = h("div", { class: "digits" });
      const tubes = h("div", { class: "digits" });
      let rendered = [];

      function redraw() {
        if (!loaded) return;
        rendered = renderDigits(`"${family}"`, { size: size.value / 100, color: color.value, glow: +glow.value, background: back.value });
        const show = (canvases, scale) => canvases.map((from) => {
          const small = h("canvas", { width: Math.round(WIDTH * scale), height: Math.round(HEIGHT * scale) });
          small.getContext("2d").drawImage(from, 0, 0, small.width, small.height);
          return small;
        });
        strip.replaceChildren(...show(rendered, 0.3));
        // The six tubes of the clock, with the time they would show.
        tubes.replaceChildren(...show([1, 2, 3, 4, 5, 6].map((digit) => rendered[digit % 10]), 0.34));
        source = async () => {
          const images = [];
          for (const from of rendered) {
            const bytes = await toBytes(from, 0.9);
            assertBaseline(bytes);
            images.push(bytes);
          }
          return images;
        };
      }
      async function load(from) {
        try {
          const font = new FontFace(family, from);
          await font.load();
          document.fonts.add(font);
          loaded = true;
          redraw();
        } catch (error) {
          loaded = false;
          source = null;
          toast("The font could not be loaded", true);
        }
      }
      const fromList = () => load(`url(https://cdn.jsdelivr.net/fontsource/fonts/${select.value}@latest/latin-400-normal.woff2)`);
      select.addEventListener("change", fromList);
      file.addEventListener("change", async () => file.files[0] && load(await file.files[0].arrayBuffer()));
      for (const control of [size, glow, color, back]) control.addEventListener("input", redraw);
      source = null;
      fromList();
      return h("div", {},
        h("div", { class: "row" }, select, h("button", { class: "btn", onclick: () => file.click() }, "Use a font file"), file),
        h("div", { class: "row" }, h("span", { class: "muted" }, "Size"), size, h("span", { class: "muted" }, "Glow"), glow),
        h("div", { class: "row" }, h("span", { class: "muted" }, "Digits"), color, h("span", { class: "muted" }, "Background"), back),
        h("p", { class: "muted", style: "margin-top:14px" }, "Preview"), strip, tubes,
        h("p", { class: "muted", style: "margin-top:14px" }, "The fonts come from Fontsource, with free licenses, and load from the internet into your browser. A font file of your own is only used here. Check that its license lets you share a face made from it."));
    }

    const modes = { font: fontPanel, images: imagesPanel };
    const buttons = {};
    const choose = (mode) => {
      body.replaceChildren(modes[mode]());
      for (const [key, button] of Object.entries(buttons)) button.classList.toggle("on", key === mode);
    };
    for (const [key, label] of [["font", "From a font"], ["images", "From ten images"]]) {
      buttons[key] = h("button", { class: "btn mode", onclick: () => choose(key) }, label);
    }
    const box = card("Create a clock face",
      h("p", { class: "muted" }, "A face is ten pictures, one per digit, fitted to the 135 × 240 tubes. Draw them from a font, or bring ten images. The clock holds up to five faces, one of them built into the firmware."),
      h("div", { class: "row" }, name), h("div", { class: "row" }, author),
      h("div", { class: "row" }, h("span", { class: "muted" }, "License"), license), h("div", { class: "row" }, about),
      h("div", { class: "row", style: "margin-top:14px" }, buttons.font, buttons.images), body,
      h("div", { class: "row", style: "margin-top:14px" }, h("button", { class: "btn primary", onclick: create }, "Create")),
      result);
    choose("font");
    return box;
  }

  return { build, renderDigits, imageToJpeg, FONTS };
})();
