// The page of the clock. It runs in the browser, and everything it does goes through the API in docs/api.md.
(() => {
  "use strict";
  const Store = DekatronStore;
  const Zones = DekatronZones;

  const MAX_FACES = 5;
  const BUILT_IN = ["Nixie"]; // the face of the firmware, which the store does not hold
  const FACE_COLORS = { Nixie: "#ff8a2a" };
  // A theme is these four animations, and the clock always needs all of them.
  const ROLES = [["boot", "Startup"], ["connecting", "Connecting"], ["online", "Connected"], ["countdown_end", "Countdown finale"]];
  const VIEWS = ["clock", "style", "settings"];
  const TITLES = { clock: "Clock", style: "Style", settings: "Settings" };
  const ICONS = {
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    style: '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z"/><path d="M19 16l.8 2.2L22 19l-2.2.8L19 22l-.8-2.2L16 19l2.2-.8z"/>',
    settings: '<path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/>',
    play: '<path d="M8 5l11 7-11 7z"/>',
    moon: '<path d="M20 14.5A8 8 0 019.5 4 8 8 0 1020 14.5z"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/>',
  };

  const $ = (selector, root = document) => root.querySelector(selector);
  const h = (tag, attributes = {}, ...children) => {
    const element = document.createElement(tag);
    for (const [name, value] of Object.entries(attributes)) {
      if (name === "class") element.className = value;
      else if (name.startsWith("on")) element.addEventListener(name.slice(2), value);
      else if (value === true) element.setAttribute(name, "");
      else if (value !== false && value != null) element.setAttribute(name, value);
    }
    for (const child of children.flat()) element.append(child?.nodeType ? child : document.createTextNode(child ?? ""));
    return element;
  };
  const icon = (name) => {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.innerHTML = ICONS[name];
    return svg;
  };

  const app = {
    state: null,
    offline: false,
    view: VIEWS.includes(location.hash.slice(1)) ? location.hash.slice(1) : "clock",
    password: sessionStorage.getItem("dekatron-password"),
    working: null, // the store as the page has it, which may differ from the clock's
    dirty: false,
    pendingSize: 0,
    loadingStore: false,
    previews: new Map(),
    busy: false,
    shown: "",
  };

  // ---- light and dark -------------------------------------------------------------------------

  const stored = (key) => { try { return localStorage.getItem(key); } catch (error) { return null; } };
  const store = (key, value) => { try { localStorage.setItem(key, value); } catch (error) { /* storage can be off */ } };
  const isDark = () => document.documentElement.dataset.theme === "dark";
  const setMode = (dark) => { document.documentElement.dataset.theme = dark ? "dark" : "light"; };
  const system = matchMedia("(prefers-color-scheme: dark)");
  setMode(stored("dekatron-mode") ? stored("dekatron-mode") === "dark" : system.matches);
  system.addEventListener("change", (event) => { if (!stored("dekatron-mode")) { setMode(event.matches); render(); } });

  // ---- talking to the clock -------------------------------------------------------------------

  const authorization = () =>
    app.password === null ? null : "Basic " + btoa(unescape(encodeURIComponent("admin:" + app.password)));

  // Several calls can be refused at once. They share one question, and all of them go again with the answer.
  let asking = null;
  function askPassword(wasWrong) {
    if (asking) return asking;
    asking = new Promise((resolve) => {
      const input = h("input", { type: "password", autocomplete: "current-password", class: "grow" });
      const dialog = h("dialog", {},
        h("form", { method: "dialog", onsubmit: () => {
          app.password = input.value;
          sessionStorage.setItem("dekatron-password", input.value);
          dialog.close();
          dialog.remove();
          asking = null;
          resolve();
        } },
        h("h3", {}, "Password of the clock"), h("div", { class: "row" }, input),
        h("p", { class: "muted" }, wasWrong ? "Wrong password" : ""),
        h("div", { class: "row" }, h("button", { class: "btn primary", type: "submit" }, "Continue"))));
      document.body.append(dialog);
      dialog.showModal();
      input.focus();
    });
    return asking;
  }

  async function request(method, path, body) {
    for (;;) {
      const headers = {};
      let payload;
      if (method === "POST") headers["X-Dekatron"] = "1";
      if (authorization()) headers.Authorization = authorization();
      if (body instanceof Uint8Array) {
        payload = body;
        headers["Content-Type"] = "application/octet-stream";
      } else if (body !== undefined) {
        payload = JSON.stringify(body);
        headers["Content-Type"] = "application/json";
      }
      const response = await fetch(path, { method, headers, body: payload });
      if (response.status !== 401) return response;
      await askPassword(app.password !== null);
    }
  }

  async function call(method, path, body) {
    const response = await request(method, path, body);
    const answer = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(answer.error || `error ${response.status}`);
    return answer;
  }

  // The body of a store goes up with a progress bar, which fetch cannot give.
  function upload(bytes, onProgress) {
    return new Promise((resolve, reject) => {
      const send = () => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/store");
        xhr.setRequestHeader("X-Dekatron", "1");
        xhr.setRequestHeader("Content-Type", "application/octet-stream");
        if (authorization()) xhr.setRequestHeader("Authorization", authorization());
        xhr.upload.onprogress = (event) => event.lengthComputable && onProgress(event.loaded / event.total);
        xhr.onerror = () => reject(new Error("network"));
        xhr.onload = async () => {
          let answer = {};
          try { answer = JSON.parse(xhr.responseText); } catch (error) { /* an answer that is not JSON has no detail */ }
          if (xhr.status === 401) {
            await askPassword(app.password !== null);
            send();
          } else if (xhr.status === 200) resolve(answer);
          else reject(new Error(answer.error || `error ${xhr.status}`));
        };
        xhr.send(bytes);
      };
      send();
    });
  }

  let toastTimer;
  function toast(message, bad = false) {
    $(".toast")?.remove();
    document.body.append(h("div", { class: "toast" + (bad ? " bad" : "") }, message));
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => $(".toast")?.remove(), bad ? 6000 : 2500);
  }

  async function act(work, done) {
    try {
      await work();
      if (done) toast(done);
    } catch (error) {
      toast(error.message, true);
    }
    await refresh();
  }

  // ---- the state of the clock -----------------------------------------------------------------

  const signature = () => JSON.stringify([
    app.view, app.dirty, app.working && app.working.entries.map((entry) => entry.name),
    app.state && [app.state.faces, app.state.clips, app.state.settings, app.state.store.size, app.state.store.status,
      /^countdown/.test(app.state.animation)],
  ]);

  async function refresh() {
    if (app.busy) return;
    try {
      app.state = await call("GET", "/api/state");
      app.offline = false;
    } catch (error) {
      app.offline = true;
    }
    updateStatus();
    // The page is drawn again only when something changed, and never under the hands of someone typing.
    if (signature() !== app.shown && !document.activeElement?.matches("input, select")) render();
  }

  const clockText = (seconds) => `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;

  function updateStatus() {
    const status = $("#status");
    if (status) {
      status.textContent = app.offline ? "Not reachable" : app.state ? app.state.name : "";
      status.classList.toggle("off", app.offline);
    }
    const left = $("#left");
    if (left && app.state) {
      const match = app.state.animation.match(/^countdown (\d+)\/(\d+) s/);
      left.textContent = match ? clockText(Math.max(0, +match[2] - +match[1])) : "";
    }
  }

  // ---- the store, as the page holds it --------------------------------------------------------

  function setWorking(parsed, dirty) {
    app.working = parsed;
    app.dirty = dirty;
    for (const links of app.previews.values()) links.forEach((link) => URL.revokeObjectURL(link));
    app.previews.clear();
    for (const entry of parsed.entries) {
      if (entry.kind === Store.KIND_FACE) {
        app.previews.set(entry.name, [1, 2, 3, 4].map((digit) => URL.createObjectURL(new Blob([entry.images[digit]], { type: "image/jpeg" }))));
      }
    }
    app.pendingSize = 0;
    try { app.pendingSize = parsed.entries.length ? Store.build(parsed.entries).length : 0; } catch (error) { /* shown when applying */ }
  }

  async function loadStore() {
    if (app.working || app.loadingStore || !app.state) return;
    if (!app.state.store.ready) {
      setWorking({ entries: [], size: 0 }, false);
      return;
    }
    app.loadingStore = true;
    try {
      const response = await request("GET", "/api/store");
      if (!response.ok) throw new Error(`error ${response.status}`);
      setWorking(Store.parse(new Uint8Array(await response.arrayBuffer())), false);
      render();
    } catch (error) {
      toast(error.message, true);
    } finally {
      app.loadingStore = false;
    }
  }

  const storeFaces = () => (app.working ? app.working.entries.filter((entry) => entry.kind === Store.KIND_FACE) : []);

  function edit(entries) {
    setWorking({ entries, size: 0 }, true);
    render();
  }

  async function apply() {
    let bytes;
    try {
      bytes = Store.build(app.working.entries, app.state.store.capacity);
      Store.parse(bytes); // what is sent has been read back the way the clock will read it
    } catch (error) {
      return toast(error.message, true);
    }
    const progress = h("progress", { max: 1, value: 0 });
    const shown = dialog("Sending to the clock…", progress, []);
    app.busy = true;
    try {
      await upload(bytes, (fraction) => { progress.value = fraction; });
      app.busy = false;
      setWorking(Store.parse(bytes), false);
      toast("The clock has the new store");
    } catch (error) {
      toast(error.message, true);
    } finally {
      app.busy = false;
      shown.close();
    }
    await refresh();
    render();
  }

  function confirmApply() {
    const box = dialog("Send the changes to the clock?",
      h("p", { class: "muted" }, "It takes about ten seconds, and the tubes move more slowly meanwhile. The file is checked first: one damaged on the way would leave the clock with no store."), [
        h("button", { class: "btn primary", onclick: () => { box.close(); apply(); } }, "Send"),
        h("button", { class: "btn", onclick: () => box.close() }, "Cancel"),
      ]);
  }

  async function download() {
    try {
      const response = await request("GET", "/api/store");
      if (!response.ok) throw new Error(`error ${response.status}`);
      const link = h("a", { href: URL.createObjectURL(await response.blob()), download: "dekatron-backup.bin" });
      document.body.append(link);
      link.click();
      link.remove();
    } catch (error) {
      toast(error.message, true);
    }
  }

  // ---- the pieces of the page -----------------------------------------------------------------

  function dialog(title, content, buttons) {
    const box = h("dialog", {}, h("h3", {}, title), content, h("div", { class: "row" }, buttons));
    document.body.append(box);
    box.showModal();
    return { close: () => { box.close(); box.remove(); } };
  }

  // A title can be a pair: the name of the card, and something small on its right.
  const card = (title, ...content) => h("section", { class: "card" },
    h("h2", {}, Array.isArray(title) ? [title[0], h("span", {}, title[1])] : title), content);

  function pendingBar() {
    if (!app.dirty) return "";
    return h("div", { class: "bar" }, h("span", {}, "The clock does not have these changes yet"), h("span", { class: "row" },
      h("button", { class: "btn primary", onclick: confirmApply }, "Apply"),
      h("button", { class: "btn", onclick: () => { app.working = null; app.dirty = false; refresh().then(loadStore); } }, "Discard")));
  }

  // ---- clock -----------------------------------------------------------------------------------

  // The faces in their five slots: the ones of the firmware, then those of the store.
  function faceSlots() {
    const live = app.state.faces;
    const builtIn = live.filter((name) => BUILT_IN.includes(name)).map((name) => ({ name, builtIn: true, live: true }));
    const others = app.working
      ? storeFaces().map((entry) => ({ name: entry.name, entry, live: live.includes(entry.name) }))
      : live.filter((name) => !BUILT_IN.includes(name)).map((name) => ({ name, live: true }));
    return [...builtIn, ...others];
  }

  function faceSlot(face, state) {
    const preview = app.previews.get(face.name);
    const content = preview
      ? h("img", { src: preview[1], alt: "" })
      : h("span", { style: `color:${FACE_COLORS[face.name] || "#ffb020"};text-shadow:0 0 10px currentColor` }, "12");
    const pick = h("button", { class: "slot" + (state.settings.face === face.name ? " on" : "") + (face.live ? "" : " pending"), "aria-label": face.name,
      onclick: () => (face.live ? act(() => call("POST", "/api/settings", { face: face.name })) : toast("Apply the changes first", true)) }, content);
    return h("div", { class: "cell" }, pick,
      face.builtIn ? "" : h("button", { class: "x", "aria-label": `Remove ${face.name}`, onclick: () => edit(app.working.entries.filter((entry) => entry !== face.entry)) }, "×"));
  }

  const go = (view) => { location.hash = view; };

  function viewClock() {
    const state = app.state;
    loadStore();
    const faces = faceSlots();
    const slots = [...Array(MAX_FACES).keys()].map((index) => faces[index]
      ? faceSlot(faces[index], state)
      : h("div", { class: "cell" }, h("button", { class: "slot empty", "aria-label": "Add a face", onclick: () => go("style") }, "+")));
    const seconds = h("input", { type: "number", min: 1, max: 3600, value: 10, "aria-label": "Seconds" });
    const minutes = h("input", { type: "number", min: 1, max: 60, value: 5, "aria-label": "Minutes" });
    const running = /^countdown/.test(state.animation);

    return h("div", {},
      card(["Clock face", `${MAX_FACES - faces.length} of ${MAX_FACES} free`], h("div", { class: "slots" }, slots),
        h("div", { class: "names" }, [...Array(MAX_FACES).keys()].map((index) => h("span", {}, faces[index] ? faces[index].name : "free")))),
      card("Countdown",
        h("div", { class: "row" }, h("span", { class: "muted" }, "Minutes"), minutes,
          h("button", { class: "btn primary", onclick: () => act(() => call("POST", "/api/play", { type: "effect", name: "countdown", duration: Math.max(1, +minutes.value || 5) * 60 })) }, "Start"),
          running ? h("button", { class: "btn", onclick: () => act(() => call("POST", "/api/stop")) }, "Cancel") : ""),
        running ? h("p", { class: "count", id: "left" }) : ""),
      card("Effects",
        h("div", { class: "row" }, h("span", { class: "muted" }, "Seconds"), seconds,
          h("button", { class: "btn", onclick: () => act(() => call("POST", "/api/stop")) }, "Stop")),
        h("div", { class: "chips", style: "margin-top:12px" }, ["matrix", "glitch", "sweep", "pulse"].map((name) =>
          h("button", { class: "chip", onclick: () => act(() => call("POST", "/api/play", { type: "effect", name, duration: +seconds.value || 10 })) }, name)))));
  }

  // ---- style -----------------------------------------------------------------------------------

  const themeEntry = () => (app.working ? app.working.entries.find((entry) => entry.kind === Store.KIND_THEME) : null);

  function viewTheme() {
    const clips = app.state.clips;
    const fileInput = h("input", { type: "file", hidden: true, onchange: async (event) => {
      try {
        const parsed = Store.parse(new Uint8Array(await event.target.files[0].arrayBuffer()));
        const wanted = ROLES.map(([name]) => parsed.entries.find((entry) => entry.kind === Store.KIND_CLIP && entry.name === name));
        if (wanted.includes(undefined)) throw new Error("That is not a theme: it needs boot, connecting, online and countdown_end");
        const named = parsed.entries.find((entry) => entry.kind === Store.KIND_THEME);
        // The theme of the file takes the place of the one of the clock, and the faces stay.
        edit([...storeFaces(), ...(named ? [named] : []), ...wanted]);
      } catch (error) {
        toast(error.message, true);
      }
    } });
    const rows = ROLES.map(([name, label]) => {
      const clip = clips.find((candidate) => candidate.name === name);
      return h("div", { class: "item" },
        clip ? h("button", { class: "round", "aria-label": `Play ${label}`, onclick: () => act(() => call("POST", "/api/play", { type: "clip", name, loop: 1 })) }, icon("play")) : "",
        h("div", { class: "grow" }, h("div", { class: "name" }, label),
          h("div", { class: "muted" }, clip ? `${(clip.frames / clip.fps).toFixed(1)} s · ${clip.fps} fps` : "Missing")));
    });
    const entry = themeEntry();
    const info = entry ? Store.themeInfo(entry) : {};
    const bytes = app.working ? app.working.entries.filter((candidate) => candidate.kind === Store.KIND_CLIP)
      .reduce((total, candidate) => total + candidate.images.reduce((sum, image) => sum + image.length, 0), 0) : 0;
    const details = [info.author && `by ${info.author}`, info.license, bytes ? kilobytes(bytes) : ""].filter(Boolean).join(" · ");
    return card("Theme",
      h("div", { class: "row between" },
        h("div", {}, h("div", { class: "big" }, entry ? entry.name : "No theme"), h("div", { class: "muted" }, details)),
        h("button", { class: "btn primary", onclick: () => fileInput.click() }, "Change theme"), fileInput),
      info.description ? h("p", { class: "muted", style: "margin-top:10px" }, info.description) : "",
      h("p", { class: "muted", style: "margin:12px 0 14px" }, "A theme is a set of four animations, and the clock always needs all four. Changing it replaces them together."),
      h("div", {}, rows));
  }

  function imageToJpeg(file) {
    return new Promise((resolve, reject) => {
      const link = URL.createObjectURL(file);
      const image = new Image();
      image.onerror = () => { URL.revokeObjectURL(link); reject(new Error(`${file.name} is not an image`)); };
      image.onload = () => {
        URL.revokeObjectURL(link);
        const canvas = h("canvas", { width: 135, height: 240 });
        const context = canvas.getContext("2d");
        context.fillStyle = "#000";
        context.fillRect(0, 0, 135, 240);
        const scale = Math.min(135 / image.naturalWidth, 240 / image.naturalHeight);
        const width = Math.max(1, Math.round(image.naturalWidth * scale));
        const height = Math.max(1, Math.round(image.naturalHeight * scale));
        context.imageSmoothingQuality = "high";
        context.drawImage(image, (135 - width) >> 1, (240 - height) >> 1, width, height);
        canvas.toBlob(async (blob) => {
          if (!blob) return reject(new Error("That image cannot be used"));
          const bytes = new Uint8Array(await blob.arrayBuffer());
          // The clock cannot read a progressive JPEG, which is marked SOF2 where a baseline one has SOF0.
          for (let i = 2; i + 1 < bytes.length; ) {
            if (bytes[i] !== 0xff) { i++; continue; }
            if (bytes[i + 1] === 0xc2) return reject(new Error("The clock reads only baseline JPEG"));
            if (bytes[i + 1] === 0xc0) break;
            i += 2 + ((bytes[i + 2] << 8) | bytes[i + 3]);
          }
          resolve(bytes);
        }, "image/jpeg", 0.88);
      };
      image.src = link;
    });
  }

  // A face goes to the working copy of the store, and from there to the clock when it is applied.
  function addFace(name, images) {
    if (!name) return toast("Give the face a name", true);
    if (faceSlots().length >= MAX_FACES) return toast("All five slots are in use. Remove a face first", true);
    if (faceSlots().some((face) => face.name === name)) return toast("A face with that name already exists", true);
    try {
      const entries = [...app.working.entries, Store.face(name, images)];
      Store.build(entries, app.state.store.capacity);
      edit(entries);
      go("clock");
    } catch (error) {
      toast(error.message, true);
    }
  }

  function imagesPanel(nameInput) {
    const picked = new Map();
    const status = h("p", { class: "muted" });
    const create = h("button", { class: "btn primary", disabled: true }, "Add");
    const input = h("input", { type: "file", accept: "image/*", multiple: true, hidden: true });
    const update = () => {
      const missing = [...Array(10).keys()].filter((digit) => !picked.has(digit));
      status.textContent = picked.size ? `${picked.size} of 10 digits${missing.length ? `, missing ${missing.join(", ")}` : ""}` : "";
      create.disabled = missing.length > 0;
    };
    const take = (files) => {
      for (const file of files) {
        const match = file.name.replace(/\.[^.]*$/, "").match(/(?:^|\D)(\d)(?:\D|$)/);
        if (match) picked.set(+match[1], file);
      }
      update();
    };
    input.addEventListener("change", () => take(input.files));
    create.addEventListener("click", async () => {
      try {
        addFace(nameInput.value.trim(), await Promise.all([...Array(10).keys()].map((digit) => imageToJpeg(picked.get(digit)))));
      } catch (error) {
        toast(error.message, true);
      }
    });
    const panel = h("div", {},
      h("div", { class: "row" }, h("button", { class: "btn", onclick: () => input.click() }, "Choose ten images"), input, create), status,
      h("p", { class: "muted" }, "Name the files 0 to 9, such as 0.png or digit-7.jpg. Each is fitted to the 135×240 tube on black. You can also drop them here."));
    panel.addEventListener("dragover", (event) => event.preventDefault());
    panel.addEventListener("drop", (event) => { event.preventDefault(); take(event.dataTransfer.files); });
    return panel;
  }

  // Free fonts of Fontsource, which serves each as a file under cdn.jsdelivr.net.
  const FONTS = [
    ["orbitron", "Orbitron"], ["share-tech-mono", "Share Tech Mono"], ["audiowide", "Audiowide"], ["bungee", "Bungee"],
    ["monoton", "Monoton"], ["major-mono-display", "Major Mono Display"], ["syncopate", "Syncopate"], ["bebas-neue", "Bebas Neue"],
    ["oswald", "Oswald"], ["roboto-mono", "Roboto Mono"], ["space-mono", "Space Mono"], ["playfair-display", "Playfair Display"],
    ["press-start-2p", "Press Start 2P"], ["vt323", "VT323"], ["black-ops-one", "Black Ops One"], ["rubik-mono-one", "Rubik Mono One"],
  ];

  // Draws the ten digits in a font, each on its own 135 x 240 canvas, on one baseline so the digits do not jump.
  function renderDigits(family, { size, color, glow, background }) {
    const probe = h("canvas", { width: 10, height: 10 }).getContext("2d");
    probe.font = `100px ${family}`;
    const zero = probe.measureText("0");
    const inkHeight = zero.actualBoundingBoxAscent + zero.actualBoundingBoxDescent || 100;
    let pixels = (100 * (240 * size)) / inkHeight;
    const widest = () => Math.max(...[...Array(10).keys()].map((digit) => {
      probe.font = `${pixels}px ${family}`;
      const ink = probe.measureText(String(digit));
      return ink.actualBoundingBoxLeft + ink.actualBoundingBoxRight;
    }));
    if (widest() > 123) pixels *= 123 / widest();
    probe.font = `${pixels}px ${family}`;
    const base = probe.measureText("0");
    const baseline = (240 + base.actualBoundingBoxAscent - base.actualBoundingBoxDescent) / 2;
    return [...Array(10).keys()].map((digit) => {
      const canvas = h("canvas", { width: 135, height: 240 });
      const context = canvas.getContext("2d");
      context.fillStyle = background;
      context.fillRect(0, 0, 135, 240);
      context.font = `${pixels}px ${family}`;
      context.textBaseline = "alphabetic";
      context.textAlign = "left";
      const ink = context.measureText(String(digit));
      const x = 67.5 - (ink.actualBoundingBoxRight - ink.actualBoundingBoxLeft) / 2;
      context.fillStyle = color;
      context.shadowColor = color;
      for (const blur of glow ? [glow, glow / 2] : [0]) {
        context.shadowBlur = blur;
        context.fillText(String(digit), x, baseline);
      }
      return canvas;
    });
  }

  function fontPanel(nameInput) {
    const family = `dk-face-${Math.random().toString(36).slice(2, 8)}`;
    let loaded = false;
    const select = h("select", { class: "grow", "aria-label": "Font" }, FONTS.map(([id, label]) => h("option", { value: id }, label)));
    const file = h("input", { type: "file", accept: ".ttf,.otf,.woff,.woff2", hidden: true });
    const size = h("input", { type: "range", min: 40, max: 90, value: 62, "aria-label": "Size" });
    const glow = h("input", { type: "range", min: 0, max: 30, value: 12, "aria-label": "Glow" });
    const color = h("input", { type: "color", value: "#ffb020", "aria-label": "Digit color" });
    const back = h("input", { type: "color", value: "#000000", "aria-label": "Background color" });
    const strip = h("div", { class: "digits" });
    const tubes = h("div", { class: "digits tubes" });
    const add = h("button", { class: "btn primary", disabled: true }, "Add");
    let rendered = [];

    function redraw() {
      if (!loaded) return;
      rendered = renderDigits(`"${family}"`, { size: size.value / 100, color: color.value, glow: +glow.value, background: back.value });
      const show = (canvases, scale) => canvases.map((source) => {
        const canvas = h("canvas", { width: Math.round(135 * scale), height: Math.round(240 * scale) });
        canvas.getContext("2d").drawImage(source, 0, 0, canvas.width, canvas.height);
        return canvas;
      });
      strip.replaceChildren(...show(rendered, 0.3));
      // The six tubes of the clock, with the time they would show.
      tubes.replaceChildren(...show([1, 2, 3, 4, 5, 6].map((digit) => rendered[digit % 10]), 0.34));
      add.disabled = false;
    }
    async function load(source) {
      try {
        const font = new FontFace(family, source);
        await font.load();
        document.fonts.add(font);
        loaded = true;
        redraw();
      } catch (error) {
        loaded = false;
        add.disabled = true;
        toast("The font could not be loaded", true);
      }
    }
    const fromList = () => load(`url(https://cdn.jsdelivr.net/fontsource/fonts/${select.value}@latest/latin-400-normal.woff2)`);
    select.addEventListener("change", fromList);
    file.addEventListener("change", async () => file.files[0] && load(await file.files[0].arrayBuffer()));
    for (const control of [size, glow, color, back]) control.addEventListener("input", redraw);
    add.addEventListener("click", async () => {
      try {
        const images = await Promise.all(rendered.map(async (canvas) => {
          const blob = await new Promise((resolve) => canvas.toBlob(resolve, "image/jpeg", 0.9));
          return new Uint8Array(await blob.arrayBuffer());
        }));
        addFace(nameInput.value.trim() || select.options[select.selectedIndex].text.slice(0, 15), images);
      } catch (error) {
        toast(error.message, true);
      }
    });
    fromList();
    return h("div", {},
      h("div", { class: "row" }, select, h("button", { class: "btn", onclick: () => file.click() }, "Use a font file"), file),
      h("div", { class: "row" }, h("span", { class: "muted" }, "Size"), size, h("span", { class: "muted" }, "Glow"), glow),
      h("div", { class: "row" }, h("span", { class: "muted" }, "Digits"), color, h("span", { class: "muted" }, "Background"), back, add),
      h("p", { class: "muted", style: "margin-top:12px" }, "Preview"), strip, tubes,
      h("p", { class: "muted", style: "margin-top:12px" }, "The fonts come from Fontsource, with free licences, and load from the internet into your browser, not the clock. A file of your own is only used here."));
  }

  function cardStudio() {
    if (faceSlots().length >= MAX_FACES) {
      return card("New face", h("p", { class: "muted" }, "All five slots are in use. Remove a face from the Clock tab to make room."));
    }
    const name = h("input", { type: "text", maxlength: 15, placeholder: "Name of the face", class: "grow" });
    const body = h("div", { style: "margin-top:12px" });
    const modes = { images: () => imagesPanel(name), font: () => fontPanel(name) };
    const buttons = {};
    const choose = (mode) => {
      body.replaceChildren(modes[mode]());
      for (const [key, button] of Object.entries(buttons)) button.classList.toggle("on", key === mode);
    };
    for (const [key, label] of [["font", "From a font"], ["images", "From images"]]) {
      buttons[key] = h("button", { class: "chip", onclick: () => choose(key) }, label);
    }
    const box = card("New face", h("div", { class: "row" }, name), h("div", { class: "chips", style: "margin-top:12px" }, buttons.font, buttons.images), body);
    choose("font");
    return box;
  }

  function viewStyle() {
    loadStore();
    return h("div", {}, viewTheme(), app.working ? cardStudio() : card("New face", h("p", { class: "muted" }, "Loading…")));
  }

  // ---- settings --------------------------------------------------------------------------------

  const kilobytes = (bytes) => (bytes >= 1048576 ? (bytes / 1048576).toFixed(2) + " MB" : Math.round(bytes / 1024) + " KB");

  function viewSettings() {
    const state = app.state;
    const current = state.settings.timezone;
    // Many cities share a rule, so the one the person picked is remembered to show it again.
    const remembered = Zones.list.find((zone) => zone.id === stored("dekatron-zone") && zone.tz === current);
    const known = remembered || Zones.byTz(current);
    const select = h("select", { class: "grow", "aria-label": "Timezone" },
      h("option", { value: "" }, current ? "Other (POSIX)" : "Not set: the one it was built with"),
      Object.entries(Zones.groups).map(([group, zones]) =>
        h("optgroup", { label: group }, zones.map(([id, tz]) => h("option", { value: tz, selected: tz === current && known?.id === id }, id.replace(/_/g, " "))))));
    const custom = h("input", { type: "text", value: known ? "" : current, placeholder: "A POSIX timezone, such as CET-1CEST,M3.5.0,M10.5.0/3", class: "grow", maxlength: 47 });
    select.addEventListener("change", () => { custom.value = select.value; });
    const save = () => {
      const zone = Zones.list.find((candidate) => candidate.tz === (custom.value || select.value).trim() && select.options[select.selectedIndex].text === candidate.id.replace(/_/g, " "));
      store("dekatron-zone", zone ? zone.id : "");
      return act(() => call("POST", "/api/settings", { timezone: (custom.value || select.value).trim() }), "Saved");
    };
    const detected = Zones.detect();
    const fresh = h("input", { type: "password", placeholder: "New password", autocomplete: "new-password", class: "grow", maxlength: 64 });
    const restore = h("input", { type: "file", hidden: true, onchange: async (event) => {
      try {
        setWorking(Store.parse(new Uint8Array(await event.target.files[0].arrayBuffer())), true);
        toast("Backup loaded. Apply it to send it to the clock");
        render();
      } catch (error) {
        toast(error.message, true);
      }
    } });
    const uptime = state.uptime_s;

    return h("div", {},
      card("Time",
        h("div", { class: "row" }, select), h("div", { class: "row" }, custom),
        h("div", { class: "row" },
          h("button", { class: "btn primary", onclick: save }, "Save"),
          detected ? h("button", { class: "btn", onclick: () => { store("dekatron-zone", detected.id); act(() => call("POST", "/api/settings", { timezone: detected.tz }), "Saved"); } }, `Use ${detected.id}`) : ""),
        h("p", { class: "muted" }, current || "Not set")),
      card("Password",
        h("p", { class: "muted" }, state.settings.password ? "A password is set." : "Without one, anybody on your network can change the clock."),
        h("div", { class: "row" }, fresh,
          h("button", { class: "btn primary", onclick: () => fresh.value && act(async () => {
            await call("POST", "/api/password", { password: fresh.value });
            app.password = fresh.value;
            sessionStorage.setItem("dekatron-password", fresh.value);
          }, "Saved") }, "Set"),
          state.settings.password ? h("button", { class: "btn danger", onclick: () => act(async () => {
            await call("POST", "/api/password", { password: "" });
            app.password = null;
            sessionStorage.removeItem("dekatron-password");
          }, "Saved") }, "Remove") : "")),
      card("Backup",
        h("p", { class: "muted" }, `The faces and the theme of the clock, ${kilobytes(state.store.size)}. Keep a copy before trying things.`),
        h("div", { class: "row" },
          h("button", { class: "btn", disabled: !state.store.ready, onclick: download }, "Download"),
          h("button", { class: "btn", onclick: () => restore.click() }, "Restore from a file"), restore)),
      card("About",
        h("p", { class: "muted" }, `${state.name} · ESPHome ${state.esphome}`),
        h("p", { class: "muted" }, `Up ${Math.floor(uptime / 3600)} h ${Math.floor(uptime / 60) % 60} min · ${kilobytes(state.free_heap)} of memory free`)));
  }

  // ---- the page --------------------------------------------------------------------------------

  function render() {
    const views = { clock: viewClock, style: viewStyle, settings: viewSettings };
    app.shown = signature();
    const modeButton = h("button", { class: "mode", "aria-label": isDark() ? "Light mode" : "Dark mode", onclick: () => {
      setMode(!isDark());
      store("dekatron-mode", isDark() ? "dark" : "light");
      render();
    } }, icon(isDark() ? "sun" : "moon"));
    const nav = h("nav", {}, h("div", {}, VIEWS.map((name) =>
      h("button", { class: name === app.view ? "on" : "", onclick: () => go(name) }, icon(name), TITLES[name]))));
    $("#app").replaceChildren(
      h("header", {}, h("div", { class: "brand" }, "Deka", h("b", {}, "tron")), h("div", { class: "head" }, h("span", { class: "pill", id: "status" }), modeButton)),
      pendingBar(),
      app.state ? views[app.view]() : h("p", { class: "muted" }, app.offline ? "The clock is not reachable." : "Loading…"),
      nav,
    );
    updateStatus();
  }

  // The address says which tab is open, so the back button and a link to one tab work.
  window.addEventListener("hashchange", () => {
    const view = location.hash.slice(1);
    if (VIEWS.includes(view) && view !== app.view) {
      app.view = view;
      window.scrollTo(0, 0);
      render();
    }
  });

  render();
  refresh();
  setInterval(() => document.visibilityState === "visible" && refresh(), 3000);
})();
