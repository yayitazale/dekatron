// The theme creator as a page of its own. It runs in the browser and sends nothing anywhere: it makes a
// theme file, which the page of the clock takes with Change theme.
(() => {
  "use strict";
  const Store = DekatronStore;
  const ROLES = [["boot", "Startup"], ["connecting", "Connecting"], ["online", "Connected"], ["countdown_end", "Countdown finale"]];
  // The clock holds 1152 KB of faces and theme. This leaves room for four faces.
  const BUDGET = 900000;

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
  const card = (title, ...content) => h("section", { class: "card" }, h("h2", {}, title), content);
  const kilobytes = (bytes) => `${Math.round(bytes / 1024)} KB`;

  document.documentElement.dataset.theme = matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";

  function dialog(title, content, buttons) {
    const box = h("dialog", {}, h("h3", {}, title), content, h("div", { class: "row" }, buttons));
    document.body.append(box);
    box.showModal();
    return { close: () => { box.close(); box.remove(); } };
  }

  const toast = (message, bad = false) => {
    document.querySelector(".toast")?.remove();
    const element = h("div", { class: "toast" + (bad ? " bad" : "") }, message);
    document.body.append(element);
    setTimeout(() => element.remove(), bad ? 7000 : 3000);
  };

  const name = h("input", { type: "text", maxlength: 15, placeholder: "Name of the theme", class: "grow" });
  const author = h("input", { type: "text", maxlength: 40, placeholder: "Your name (optional)", class: "grow" });
  const about = h("input", { type: "text", maxlength: 160, placeholder: "A line about it (optional)", class: "grow" });
  const fit = h("select", { class: "grow", "aria-label": "Picture on the strip" },
    [h("option", { value: "fill" }, "Fill the strip, cropping"), h("option", { value: "fit" }, "Fit inside, with black around")]);
  const lights = h("select", { class: "grow", "aria-label": "Lights" },
    [h("option", { value: "follow" }, "Lights follow the picture"), h("option", { value: "off" }, "No lights")]);
  const result = h("div", {});

  const picks = ROLES.map(([role, label]) => {
    const pick = { role, label, files: null };
    const shown = h("span", { class: "muted" }, "Nothing chosen");
    const input = h("input", { type: "file", accept: "image/*,video/*", multiple: true, hidden: true });
    input.addEventListener("change", () => {
      pick.files = input.files.length ? [...input.files] : null;
      shown.textContent = pick.files ? (pick.files.length > 1 ? `${pick.files.length} images` : pick.files[0].name) : "Nothing chosen";
    });
    pick.seconds = h("input", { type: "number", min: 1, max: 60, value: label === "Connecting" ? 8 : 10, "aria-label": `${label} seconds` });
    pick.fps = h("input", { type: "number", min: 1, max: 30, value: 15, "aria-label": `${label} frames per second` });
    pick.row = h("div", { class: "item" }, h("div", { class: "grow" },
      h("div", { class: "name" }, label),
      h("div", { class: "row" }, h("button", { class: "btn", onclick: () => input.click() }, "Choose"), input, shown),
      h("div", { class: "row" }, h("span", { class: "muted" }, "Seconds"), pick.seconds, h("span", { class: "muted" }, "per second"), pick.fps)));
    return pick;
  });

  async function make() {
    result.replaceChildren();
    if (!name.value.trim()) return toast("Give the theme a name", true);
    const missing = picks.find((pick) => !pick.files);
    if (missing) return toast(`Choose a file for ${missing.label}`, true);
    const roles = picks.map((pick) => ({
      name: pick.role, label: pick.label, files: pick.files, fps: Math.min(30, Math.max(1, +pick.fps.value || 15)),
      seconds: Math.min(60, Math.max(1, +pick.seconds.value || 8)), fit: fit.value === "fit", lights: lights.value === "follow",
    }));
    const progress = h("progress", { max: 1, value: 0 });
    const text = h("p", { class: "muted" }, "Reading the files…");
    const stop = { now: false };
    const shown = dialog("Making the theme…", h("div", {}, progress, text), [h("button", { class: "btn", onclick: () => { stop.now = true; } }, "Stop")]);
    try {
      const made = await DekatronCreator.makeClips(roles, BUDGET, (message, fraction) => { text.textContent = message; progress.value = fraction; }, stop);
      const info = {};
      if (author.value.trim()) info.author = author.value.trim();
      if (about.value.trim()) info.description = about.value.trim();
      const bytes = Store.build([Store.theme(name.value.trim(), info), ...made.clips]);
      const link = h("a", { class: "btn primary", href: URL.createObjectURL(new Blob([bytes])), download: `${name.value.trim()}.bin` }, `Download ${name.value.trim()}.bin`);
      result.replaceChildren(
        h("p", {}, `Ready: ${kilobytes(bytes.length)}, with ${made.clips.map((clip) => `${clip.frames} frames`).join(", ")}.`),
        h("p", { class: "muted" }, "On the page of your clock, open Style, press Change theme and choose this file, then Apply."),
        h("div", { class: "row" }, link));
    } catch (error) {
      toast(error.message, true);
    } finally {
      shown.close();
    }
  }

  document.getElementById("app").append(
    h("header", {}, h("div", { class: "brand" }, "Deka", h("b", {}, "tron"), " themes")),
    card("Create a theme",
      h("p", { class: "muted" }, "A theme is four animations: when the clock starts, while it connects, when it is connected and at the end of a countdown. Use an animated GIF, WebP or PNG, a video, or a numbered series of images for each. The animation crosses the six tubes as one wide strip, nearly six times as wide as tall. Nothing leaves your computer."),
      h("div", { class: "row" }, name), h("div", { class: "row" }, author), h("div", { class: "row" }, about),
      h("div", { class: "row" }, fit), h("div", { class: "row" }, lights),
      h("div", {}, picks.map((pick) => pick.row)),
      h("div", { class: "row", style: "margin-top:14px" }, h("button", { class: "btn primary", onclick: make }, "Create")),
      result),
    card("Tips",
      h("p", { class: "muted" }, "Few colors and small movements make small files. The clock has room for about 1 MB of faces and theme together, and this page keeps your theme under 880 KB so four faces still fit. If it is too big, use fewer seconds or fewer frames per second."),
      h("p", { class: "muted" }, "The lights take the color of what is on each tube, and the tubes are lit left to right, so put the action where the tubes are.")));
})();
