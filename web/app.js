// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The page of the clock. It runs in the browser, and everything it does goes through the API in docs/api.md.
// It controls the clock, uploads files (themes, faces, backups, firmware) and keeps a backup. Making themes
// and faces is done on the site of the project, which cannot talk to the clock: a file is made there and
// uploaded here.
(() => {
  "use strict";
  const Store = DekatronStore;
  const Zones = DekatronZones;
  const Update = DekatronUpdate;

  const BUILT_IN = ["Nixie"]; // the face of the firmware, which the store does not hold
  const MAX_FACES = Store.MAX_FACES + BUILT_IN.length;
  const FACE_COLORS = { Nixie: "#ff8a2a" };
  // A theme is these four animations, and the clock always needs all of them.
  const ROLES = [["boot", "Startup"], ["connecting", "Connecting"], ["online", "Connected"], ["countdown_end", "Countdown finale"]];
  const VIEWS = ["clock", "files", "settings"];
  const TITLES = { clock: "Clock", files: "Files", settings: "Settings" };
  const ICONS = {
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    files: '<path d="M14 3H7a2 2 0 00-2 2v14a2 2 0 002 2h10a2 2 0 002-2V8z"/><path d="M14 3v5h5M12 17v-6M9.5 13.5L12 11l2.5 2.5"/>',
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
    store: null, // the store of the clock as the page last read it, for the faces and the theme it shows
    loadingStore: false,
    previews: new Map(),
    busy: false,
    shown: "",
    update: null, // a newer release, if the site of the project has one
    updateChecked: false,
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
  function upload(bytes, onProgress, path = "/api/store") {
    return new Promise((resolve, reject) => {
      const send = () => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", path);
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
    app.view, app.store && app.store.entries.map((entry) => entry.name), app.update && app.update.version,
    app.state && [app.state.faces, app.state.clips, app.state.settings, app.state.store.size, app.state.store.status,
      /^countdown/.test(app.state.animation), app.state.controls, app.state.version],
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
    if (app.state && !app.updateChecked) {
      app.updateChecked = true;
      checkUpdate(false).then(() => { if (signature() !== app.shown) render(); });
    }
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

  // ---- the store, as the clock has it ----------------------------------------------------------

  function setStore(parsed) {
    app.store = parsed;
    for (const links of app.previews.values()) links.forEach((link) => URL.revokeObjectURL(link));
    app.previews.clear();
    for (const entry of parsed.entries) {
      if (entry.kind === Store.KIND_FACE) {
        app.previews.set(entry.name, [1, 2, 3, 4].map((digit) => URL.createObjectURL(new Blob([entry.images[digit]], { type: "image/jpeg" }))));
      }
    }
  }

  // The store as it is in the clock now, which is what a change starts from. A clock with none has no entries.
  async function fetchStore() {
    if (!app.state.store.ready) return { entries: [], size: 0 };
    const response = await request("GET", "/api/store");
    if (!response.ok) throw new Error(`error ${response.status}`);
    return Store.parse(new Uint8Array(await response.arrayBuffer()));
  }

  async function loadStore() {
    if (app.store || app.loadingStore || !app.state) return;
    app.loadingStore = true;
    try {
      setStore(await fetchStore());
      render();
    } catch (error) {
      toast(error.message, true);
    } finally {
      app.loadingStore = false;
    }
  }

  const storeFaces = (entries = app.store ? app.store.entries : []) => entries.filter((entry) => entry.kind === Store.KIND_FACE);
  const storeTheme = (entries = app.store ? app.store.entries : []) => entries.find((entry) => entry.kind === Store.KIND_THEME);

  // Sends a new store, which replaces the whole previous one in the clock.
  async function sendStore(bytes, what) {
    try {
      Store.parse(bytes); // what is sent has been read back the way the clock will read it
    } catch (error) {
      return toast(error.message, true);
    }
    const progress = h("progress", { max: 1, value: 0 });
    const shown = dialog("Sending to the clock…", h("div", {}, progress, h("p", { class: "muted" }, "It takes about ten seconds, and the tubes move more slowly meanwhile.")), []);
    app.busy = true;
    try {
      await upload(bytes, (fraction) => { progress.value = fraction; });
      app.busy = false;
      setStore(Store.parse(bytes));
      toast(what);
    } catch (error) {
      toast(error.message, true);
    } finally {
      app.busy = false;
      shown.close();
    }
    await refresh();
    render();
  }

  function build(entries) {
    return Store.build(entries, app.state.store.capacity);
  }

  // A firmware file is checked before it is sent, and the clock checks it again before it takes it.
  async function sendFirmware(bytes) {
    const report = Store.checkBin(bytes);
    if (report.type !== "firmware" || !report.ok) {
      return toast(report.problems[0] ? report.problems[0].message : "That is not a firmware file. Use firmware.ota.bin", true);
    }
    const progress = h("progress", { max: 1, value: 0 });
    const shown = dialog("Sending the firmware…", h("div", {}, progress, h("p", { class: "muted" }, "Do not unplug the clock.")), []);
    try {
      await upload(bytes, (fraction) => { progress.value = fraction; }, "/api/firmware");
    } catch (error) {
      shown.close();
      return toast(error.message, true);
    }
    shown.close();
    const waiting = dialog("The clock restarts…", h("p", { class: "muted" }, "It takes about half a minute. If the new firmware does not start, the clock goes back to the old one."), []);
    await new Promise((done) => setTimeout(done, 8000));
    for (let tries = 0; tries < 40; tries++) {
      try {
        const answer = await fetch("/api/info", { cache: "no-store" });
        if (answer.ok) break;
      } catch (error) { /* it is still down */ }
      await new Promise((done) => setTimeout(done, 2000));
    }
    waiting.close();
    location.reload();
  }

  function saveFile(bytes, name) {
    const link = h("a", { href: URL.createObjectURL(new Blob([bytes])), download: name });
    document.body.append(link);
    link.click();
    link.remove();
  }

  const today = () => new Date().toISOString().slice(0, 10).replace(/-/g, "");

  async function downloadBackup() {
    try {
      const response = await request("GET", "/api/store");
      if (!response.ok) throw new Error(`error ${response.status}`);
      saveFile(await response.blob(), `dekatron-backup-${today()}.bin`);
    } catch (error) {
      toast(error.message, true);
    }
  }

  // The theme of the store as a file of its own, which the clock takes in Files.
  function downloadTheme() {
    try {
      const entries = app.store.entries;
      const clips = ROLES.map(([name]) => entries.find((entry) => entry.kind === Store.KIND_CLIP && entry.name === name));
      if (!storeTheme() || clips.includes(undefined)) throw new Error("The theme is not complete");
      saveFile(Store.build([storeTheme(), ...clips]), `${storeTheme().name}.theme.bin`);
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

  const kilobytes = (bytes) => (bytes >= 1048576 ? (bytes / 1048576).toFixed(2) + " MB" : Math.round(bytes / 1024) + " KB");

  // ---- the controls of the clock ---------------------------------------------------------------

  const control = (body) => act(() => call("POST", "/api/controls", body));

  const toggle = (on, label, change) => h("button", { class: "sw" + (on ? " on" : ""), role: "switch", "aria-checked": String(on), "aria-label": label, onclick: () => change(!on) });

  // The number is shown while the slider moves, and sent when it is let go.
  function slider(label, value, low, high, step, change) {
    const shown = h("span", { class: "value" }, `${value}%`);
    const input = h("input", { type: "range", min: low, max: high, step, value, "aria-label": label });
    input.addEventListener("input", () => { shown.textContent = `${input.value}%`; });
    input.addEventListener("change", () => change(+input.value));
    return h("div", { class: "row slider" }, h("span", { class: "muted" }, label), input, shown);
  }

  const SWATCHES = ["#ffb020", "#ff7a59", "#ff5fa8", "#8b7bff", "#3fb6ff", "#4fe0a0", "#ffffff"];

  function cardDisplays(controls) {
    if (controls.displays === undefined && controls.brightness === undefined) return "";
    return card("Displays",
      controls.displays === undefined ? "" : h("div", { class: "row between" }, h("span", {}, "On"), toggle(controls.displays, "Displays", (value) => control({ displays: value }))),
      controls.brightness === undefined ? "" : slider("Brightness", controls.brightness, 5, 100, 5, (value) => control({ brightness: value })));
  }

  function cardLights(controls) {
    const leds = controls.leds;
    if (!leds) return "";
    const picker = h("input", { type: "color", value: leds.color, "aria-label": "Custom color" });
    picker.addEventListener("change", () => control({ leds: { on: true, color: picker.value } }));
    return card("Lights",
      h("div", { class: "row between" }, h("span", {}, "On"), toggle(leds.on, "Lights", (value) => control({ leds: { on: value } }))),
      h("div", { class: "row" }, SWATCHES.map((color) =>
        h("button", { class: "swatch" + (leds.on && leds.color === color ? " on" : ""), style: `background:${color}`, "aria-label": `Color ${color}`, onclick: () => control({ leds: { on: true, color } }) })), picker),
      slider("Brightness", leds.brightness, 1, 100, 1, (value) => control({ leds: { on: true, brightness: value } })),
      leds.effects.length > 1 ? h("div", { class: "chips", style: "margin-top:12px" }, leds.effects.map((name) =>
        h("button", { class: "chip" + (leds.effect === name ? " on" : ""), onclick: () => control({ leds: { effect: name } }) }, name))) : "");
  }

  function cardDisplay(controls) {
    const night = controls.night;
    if (controls.hour24 === undefined && controls.transition === undefined && !night) return "";
    const hours = (value, change, label) => {
      const select = h("select", { "aria-label": label }, [...Array(24).keys()].map((hour) => h("option", { value: hour, selected: hour === value }, `${String(hour).padStart(2, "0")}:00`)));
      select.addEventListener("change", () => change(+select.value));
      return select;
    };
    return card("Display",
      controls.hour24 === undefined ? "" : h("div", { class: "row between" }, h("span", {}, "24-hour format"), toggle(controls.hour24, "24-hour format", (value) => control({ hour24: value }))),
      controls.leading_zero === undefined ? "" : h("div", { class: "row between" }, h("span", {}, "Zero before the hour"), toggle(controls.leading_zero, "Zero before the hour", (value) => control({ leading_zero: value }))),
      controls.transition === undefined ? "" : h("div", { class: "row between" }, h("span", {}, "Digit change"),
        h("span", { class: "chips" }, ["Off", "Fade"].map((name) => h("button", { class: "chip" + (controls.transition === name ? " on" : ""), onclick: () => control({ transition: name }) }, name)))),
      !night ? "" : h("div", {},
        h("div", { class: "row between" }, h("span", {}, "Night mode"), toggle(night.on, "Night mode", (value) => control({ night: { on: value } }))),
        night.start === undefined ? "" : h("div", { class: "row" }, h("span", { class: "muted" }, "From"), hours(night.start, (value) => control({ night: { start: value } }), "From"),
          h("span", { class: "muted" }, "to"), hours(night.end, (value) => control({ night: { end: value } }), "To")),
        night.brightness === undefined ? "" : slider("Dimmed to", night.brightness, 0, 100, 5, (value) => control({ night: { brightness: value } }))));
  }

  // ---- clock -----------------------------------------------------------------------------------

  // The faces in their five slots: the one of the firmware, then those of the store.
  function faceSlots() {
    const live = app.state.faces;
    return [...live.filter((name) => BUILT_IN.includes(name)), ...live.filter((name) => !BUILT_IN.includes(name))].map((name) => ({ name, builtIn: BUILT_IN.includes(name) }));
  }

  function faceSlot(face, state) {
    const preview = app.previews.get(face.name);
    const content = preview
      ? h("img", { src: preview[1], alt: "" })
      : h("span", { style: `color:${FACE_COLORS[face.name] || "#ffb020"};text-shadow:0 0 10px currentColor` }, "12");
    return h("div", { class: "cell" },
      h("button", { class: "slot" + (state.settings.face === face.name ? " on" : ""), "aria-label": face.name,
        onclick: () => act(() => call("POST", "/api/settings", { face: face.name })) }, content));
  }

  const go = (view) => { location.hash = view; };

  function viewClock() {
    const state = app.state;
    loadStore();
    const faces = faceSlots();
    const slots = [...Array(MAX_FACES).keys()].map((index) => faces[index]
      ? faceSlot(faces[index], state)
      : h("div", { class: "cell" }, h("button", { class: "slot empty", "aria-label": "Add a face", onclick: () => go("files") }, "+")));
    const seconds = h("input", { type: "number", min: 1, max: 3600, value: 10, "aria-label": "Seconds" });
    const minutes = h("input", { type: "number", min: 1, max: 60, value: 5, "aria-label": "Minutes" });
    const running = /^countdown/.test(state.animation);
    const controls = state.controls || {};

    return h("div", {},
      cardDisplays(controls),
      cardLights(controls),
      cardDisplay(controls),
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

  // ---- files -----------------------------------------------------------------------------------

  const byLine = (info) => [info.author && `by ${info.author}`, info.license].filter(Boolean).join(" · ");

  // What a file is, shown before anything changes, with what the clock will do and the way back.
  async function openFile(file) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const report = Store.checkBin(bytes);
    const names = { theme: "Theme", face: "Clock face", backup: "Backup", firmware: "Firmware" };
    const title = report.type === "unknown" ? "Not a file for the clock" : `${names[report.type]}${report.name ? `: ${report.name}` : ""}`;
    const lines = [
      report.type !== "unknown" ? h("p", { class: "muted" }, [byLine(report.info), kilobytes(bytes.length)].filter(Boolean).join(" · ")) : "",
      report.info.description ? h("p", { class: "muted" }, report.info.description) : "",
      report.problems.length ? h("ul", { class: "problems" }, report.problems.map((problem) => h("li", {}, problem.message))) : "",
      report.ok && report.warnings.length ? h("ul", { class: "notes" }, report.warnings.map((warning) => h("li", {}, warning.message))) : "",
    ];
    let box;
    const close = () => box.close();
    const cancel = h("button", { class: "btn", onclick: close }, report.ok ? "Cancel" : "Close");
    const choose = h("select", { class: "grow", "aria-label": "Slot" });
    const backupFirst = h("button", { class: "btn", onclick: downloadBackup }, "Download a backup first");
    const action = (label, work) => h("button", { class: "btn primary", onclick: () => { close(); work(); } }, label);
    const buttons = [];
    const extra = [];

    if (report.ok && report.type === "theme") {
      extra.push(h("p", { class: "muted" }, "It replaces the theme of the clock. The faces stay."));
      buttons.push(action("Install theme", () => installTheme(bytes)), backupFirst);
    } else if (report.ok && report.type === "face") {
      if (!app.store) await loadStore();
      const faces = storeFaces();
      if (faces.length < Store.MAX_FACES) choose.append(h("option", { value: "free" }, `Add as a new face (${Store.MAX_FACES - faces.length} free)`));
      for (const [index, entry] of faces.entries()) {
        choose.append(h("option", { value: String(index) }, entry.name === report.name ? `Replace ${entry.name} (same name)` : `Replace ${entry.name}`));
      }
      const same = faces.findIndex((entry) => entry.name === report.name);
      if (same >= 0) choose.value = String(same);
      extra.push(h("div", { class: "row" }, choose));
      buttons.push(action("Install face", () => installFace(bytes, choose.value)), backupFirst);
    } else if (report.ok && report.type === "backup") {
      extra.push(h("p", { class: "muted" }, "It replaces everything the clock keeps: all the faces and the theme."));
      buttons.push(action("Restore", () => sendStore(bytes, "The clock has the backup")), backupFirst);
    } else if (report.ok && report.type === "firmware") {
      if (app.state.settings.password) {
        extra.push(h("p", { class: "muted" }, "The clock checks it, restarts, and keeps the old firmware if the new one does not start."));
        buttons.push(action("Install firmware", () => sendFirmware(bytes)));
      } else {
        extra.push(h("p", { class: "muted" }, "Set a password first, in Settings. Nobody should be able to send a program to the clock without one."));
      }
    }
    box = dialog(title, h("div", {}, lines, extra), [...buttons, cancel]);
  }

  async function installTheme(bytes) {
    try {
      const incoming = Store.parse(bytes).entries;
      const clips = ROLES.map(([name]) => incoming.find((entry) => entry.kind === Store.KIND_CLIP && entry.name === name));
      const current = (await fetchStore()).entries;
      // The theme of the file takes the place of the one of the clock, and the faces stay.
      const next = [...storeFaces(current), ...incoming.filter((entry) => entry.kind === Store.KIND_THEME), ...clips];
      await sendStore(build(next), "The clock has the new theme");
    } catch (error) {
      toast(error.message, true);
    }
  }

  async function installFace(bytes, slot) {
    try {
      const face = Store.parse(bytes).entries.find((entry) => entry.kind === Store.KIND_FACE);
      const current = (await fetchStore()).entries;
      const faces = storeFaces(current);
      const replaced = slot === "free" ? undefined : faces[+slot];
      if (!replaced && faces.length >= Store.MAX_FACES) throw new Error("All the slots are in use. Choose one to replace");
      if (faces.some((entry) => entry.name === face.name && entry !== replaced)) throw new Error("A face with that name is already there. Choose it to replace");
      const next = replaced ? current.map((entry) => (entry === replaced ? face : entry)) : [...current, face];
      await sendStore(build(next), `The clock has ${face.name}`);
    } catch (error) {
      toast(error.message, true);
    }
  }

  function removeFace(entry) {
    const box = dialog(`Remove ${entry.name}?`, h("p", { class: "muted" }, "The face is deleted from the clock. Download a backup first if you want to keep it."), [
      h("button", { class: "btn danger", onclick: async () => {
        box.close();
        try {
          const next = (await fetchStore()).entries.filter((candidate) => candidate.kind !== Store.KIND_FACE || candidate.name !== entry.name);
          if (!next.length) throw new Error("The store cannot be empty: install a theme or a face first");
          await sendStore(build(next), `${entry.name} is removed`);
        } catch (error) {
          toast(error.message, true);
        }
      } }, "Remove"),
      h("button", { class: "btn", onclick: () => box.close() }, "Cancel"),
    ]);
  }

  function cardUpload() {
    const input = h("input", { type: "file", accept: ".bin", hidden: true, onchange: (event) => { const file = event.target.files[0]; event.target.value = ""; if (file) openFile(file); } });
    const box = card("Upload a file",
      h("p", { class: "muted" }, "A theme, a clock face, a backup or a firmware, as a .bin file. The page says what it is before it changes anything."),
      h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => input.click() }, "Choose a file"), input),
      h("p", { class: "muted" }, "You can also drop it here."));
    box.classList.add("drop");
    box.addEventListener("dragover", (event) => { event.preventDefault(); box.classList.add("over"); });
    box.addEventListener("dragleave", () => box.classList.remove("over"));
    box.addEventListener("drop", (event) => { event.preventDefault(); box.classList.remove("over"); if (event.dataTransfer.files[0]) openFile(event.dataTransfer.files[0]); });
    return box;
  }

  function cardInstalled() {
    if (!app.store) return card("On the clock", h("p", { class: "muted" }, "Loading…"));
    const entries = app.store.entries;
    const theme = storeTheme();
    const info = theme ? Store.themeInfo(theme) : {};
    const clips = app.state.clips;
    const rows = ROLES.map(([name, label]) => {
      const clip = clips.find((candidate) => candidate.name === name);
      return h("div", { class: "item" },
        clip ? h("button", { class: "round", "aria-label": `Play ${label}`, onclick: () => act(() => call("POST", "/api/play", { type: "clip", name, loop: 1 })) }, icon("play")) : "",
        h("div", { class: "grow" }, h("div", { class: "name" }, label),
          h("div", { class: "muted" }, clip ? `${(clip.frames / clip.fps).toFixed(1)} s · ${clip.fps} fps` : "Missing", clip && clip.leds ? h("span", { class: "tag" }, "lights") : "")));
    });
    const faces = storeFaces(entries).map((entry) => {
      const preview = app.previews.get(entry.name);
      const faceInfo = Store.faceInfo(entry);
      return h("div", { class: "item" },
        h("div", { class: "thumb" }, preview ? h("img", { src: preview[1], alt: "" }) : ""),
        h("div", { class: "grow" }, h("div", { class: "name" }, entry.name), h("div", { class: "muted" }, byLine(faceInfo) || "No author")),
        h("button", { class: "x2", "aria-label": `Remove ${entry.name}`, onclick: () => removeFace(entry) }, "×"));
    });
    return card("On the clock",
      h("div", { class: "big" }, theme ? theme.name : "No theme"),
      h("div", { class: "muted" }, [byLine(info), `store ${kilobytes(app.state.store.size)}`].filter(Boolean).join(" · ")),
      info.description ? h("p", { class: "muted", style: "margin:8px 0 0" }, info.description) : "",
      h("div", { style: "margin-top:12px" }, rows),
      h("h2", { style: "margin:18px 0 8px" }, "Faces"),
      h("div", {}, faces.length ? faces : h("p", { class: "muted" }, "Only the built-in face. Upload a face to add more.")));
  }

  function viewFiles() {
    loadStore();
    const state = app.state;
    return h("div", {},
      cardUpload(),
      cardInstalled(),
      card("Backup",
        h("p", { class: "muted" }, `The faces and the theme of the clock, ${kilobytes(state.store.size)}. Keep a copy before trying things; uploading a backup file brings it back.`),
        h("div", { class: "row" },
          h("button", { class: "btn", disabled: !state.store.ready, onclick: downloadBackup }, "Download backup"),
          h("button", { class: "btn", disabled: !storeTheme(), onclick: downloadTheme }, "Download theme"))),
      card("Make your own",
        h("p", { class: "muted" }, "Themes and faces are made on the site of the project, in your browser, and come out as a file to upload here. The site cannot talk to the clock itself."),
        h("div", { class: "row" },
          h("a", { class: "btn", href: Update.SITE + "#themes", target: "_blank", rel: "noopener" }, "Gallery of themes"),
          h("a", { class: "btn", href: Update.SITE + "creator/", target: "_blank", rel: "noopener" }, "Creator"))));
  }

  // ---- updates ---------------------------------------------------------------------------------

  // Asks the site of the project for versions.json at most every two hours, and only while this page is open.
  async function checkUpdate(force) {
    const version = app.state && app.state.version;
    if (!version) return "unknown";
    let cached = null;
    try { cached = JSON.parse(stored("dekatron-versions") || "null"); } catch (error) { /* it will be asked again */ }
    let data = cached && cached.data;
    let reached = !!cached;
    if (force || !cached || Update.due(cached.at)) {
      try {
        const control = new AbortController();
        const timer = setTimeout(() => control.abort(), 8000);
        const response = await fetch(Update.SITE + "versions.json", { cache: "no-store", signal: control.signal });
        clearTimeout(timer);
        if (!response.ok) throw new Error(`error ${response.status}`);
        data = await response.json();
        store("dekatron-versions", JSON.stringify({ at: Date.now(), data }));
        reached = true;
      } catch (error) {
        reached = false; // no internet, or the site is down: the page keeps working without the notice
      }
    }
    app.update = Update.pick(data, version, "full");
    return app.update ? "new" : reached ? "current" : "offline";
  }

  function installUpdate() {
    const release = app.update;
    const box = dialog(`Update to ${release.version}?`, h("div", {},
      release.notes ? h("p", { class: "notes-text" }, release.notes) : "",
      h("p", { class: "muted" }, "The page downloads the firmware from the site of the project, checks it, and sends it to the clock. The clock restarts, and goes back to the old firmware if the new one does not start.")), [
      h("button", { class: "btn primary", onclick: async () => {
        box.close();
        const progress = h("progress", { max: 1, value: 0 });
        const shown = dialog("Downloading the firmware…", progress, []);
        try {
          const response = await fetch(release.url);
          if (!response.ok) throw new Error(`error ${response.status}`);
          const total = +response.headers.get("content-length") || 0;
          const reader = response.body.getReader();
          const chunks = [];
          let received = 0;
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            received += value.length;
            if (total) progress.value = received / total;
          }
          const bytes = new Uint8Array(received);
          let at = 0;
          for (const chunk of chunks) { bytes.set(chunk, at); at += chunk.length; }
          if (Update.sha256(bytes) !== release.sha256) throw new Error("The download does not match its checksum, so it was not sent");
          shown.close();
          await sendFirmware(bytes);
        } catch (error) {
          shown.close();
          toast(error.message, true);
        }
      } }, "Update"),
      h("button", { class: "btn", onclick: () => box.close() }, "Later"),
    ]);
  }

  // ---- settings --------------------------------------------------------------------------------

  function cardFirmware(state) {
    const release = app.update;
    const check = h("button", { class: "btn", onclick: async () => {
      const result = await checkUpdate(true);
      toast(result === "new" ? `Version ${app.update.version} is available` : result === "current" ? "The firmware is up to date" : result === "offline" ? "The site of the project cannot be reached from this device" : "This firmware does not say its version", result === "offline");
      render();
    } }, "Check for updates");
    return card("Firmware",
      h("p", { class: "muted" }, `${state.version ? `Version ${state.version}. ` : ""}Built ${state.build || "at an unknown time"}.`),
      release ? h("div", { class: "notice" },
        h("div", { class: "name" }, `Version ${release.version} is available`), release.date ? h("div", { class: "muted" }, release.date) : "",
        state.settings.password
          ? h("div", { class: "row" }, h("button", { class: "btn primary", onclick: installUpdate }, "Update"))
          : h("p", { class: "muted" }, "Set a password first. Nobody should be able to send a program to the clock without one.")) : "",
      h("div", { class: "row" }, check),
      h("p", { class: "muted" }, "The page asks the site of the project about new versions, at most every two hours, and the clock never goes to the internet. You can also upload a firmware.ota.bin in Files."));
  }

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
    const uptime = state.uptime_s;

    return h("div", {},
      card("Time",
        h("div", { class: "row" }, select), h("div", { class: "row" }, custom),
        h("div", { class: "row" },
          h("button", { class: "btn primary", onclick: save }, "Save"),
          detected ? h("button", { class: "btn", onclick: () => { store("dekatron-zone", detected.id); act(() => call("POST", "/api/settings", { timezone: detected.tz }), "Saved"); } }, `Use ${detected.id}`) : ""),
        h("p", { class: "muted" }, current || "Not set")),
      card("Startup",
        h("div", { class: "row between" }, h("span", {}, "Play the startup animation"),
          toggle(state.settings.boot_animation !== false, "Startup animation", (value) => act(() => call("POST", "/api/settings", { boot_animation: value }))))),
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
      cardFirmware(state),
      card("About",
        h("p", { class: "muted" }, `${state.name} · ESPHome ${state.esphome}`),
        h("p", { class: "muted" }, `Up ${Math.floor(uptime / 3600)} h ${Math.floor(uptime / 60) % 60} min · ${kilobytes(state.free_heap)} of memory free`)));
  }

  // ---- the page --------------------------------------------------------------------------------

  function render() {
    const views = { clock: viewClock, files: viewFiles, settings: viewSettings };
    app.shown = signature();
    const modeButton = h("button", { class: "mode", "aria-label": isDark() ? "Light mode" : "Dark mode", onclick: () => {
      setMode(!isDark());
      store("dekatron-mode", isDark() ? "dark" : "light");
      render();
    } }, icon(isDark() ? "sun" : "moon"));
    const nav = h("nav", {}, h("div", {}, VIEWS.map((name) =>
      h("button", { class: (name === app.view ? "on" : "") + (name === "settings" && app.update ? " dot" : ""), onclick: () => go(name) }, icon(name), TITLES[name]))));
    $("#app").replaceChildren(
      h("header", {}, h("div", { class: "brand" }, "Deka", h("b", {}, "tron")), h("div", { class: "head" }, h("span", { class: "pill", id: "status" }), modeButton)),
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
