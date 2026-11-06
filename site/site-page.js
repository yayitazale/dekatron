// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The landing page: light and dark, the buttons that copy a prompt, and the gallery of themes.
(() => {
  "use strict";
  const stored = (key) => { try { return localStorage.getItem(key); } catch (error) { return null; } };
  const keep = (key, value) => { try { localStorage.setItem(key, value); } catch (error) { /* storage can be off */ } };

  const system = matchMedia("(prefers-color-scheme: dark)");
  const mode = document.getElementById("mode");
  const apply = (dark) => {
    document.documentElement.dataset.theme = dark ? "dark" : "light";
    mode.textContent = dark ? "☀" : "☾";
    mode.setAttribute("aria-label", dark ? "Light mode" : "Dark mode");
  };
  apply(stored("dekatron-mode") ? stored("dekatron-mode") === "dark" : system.matches);
  system.addEventListener("change", (event) => { if (!stored("dekatron-mode")) apply(event.matches); });
  mode.addEventListener("click", () => {
    const dark = document.documentElement.dataset.theme !== "dark";
    keep("dekatron-mode", dark ? "dark" : "light");
    apply(dark);
  });

  for (const button of document.querySelectorAll("[data-copy]")) {
    button.addEventListener("click", async () => {
      const text = document.getElementById(button.dataset.copy).textContent;
      try {
        await navigator.clipboard.writeText(text);
        button.textContent = "Copied";
      } catch (error) {
        const range = document.createRange();
        range.selectNodeContents(document.getElementById(button.dataset.copy));
        getSelection().removeAllRanges();
        getSelection().addRange(range);
        button.textContent = "Press Ctrl+C";
      }
      setTimeout(() => { button.textContent = "Copy"; }, 2500);
    });
  }

  const element = (tag, attributes = {}, ...children) => {
    const node = document.createElement(tag);
    for (const [name, value] of Object.entries(attributes)) node.setAttribute(name, value);
    node.append(...children.map((child) => (child?.nodeType ? child : document.createTextNode(child ?? ""))));
    return node;
  };

  // The gallery is index.json of the themes that the release publishes next to this page.
  async function gallery() {
    const box = document.getElementById("gallery");
    try {
      const response = await fetch("themes/index.json", { cache: "no-cache" });
      if (!response.ok) throw new Error(String(response.status));
      const { themes } = await response.json();
      if (!themes.length) throw new Error("empty");
      box.replaceChildren(...themes.map((theme) => element("article", { class: "card theme" },
        element("img", { src: `themes/${theme.preview}`, alt: `A preview of the ${theme.name} theme on the six tubes`, loading: "lazy" }),
        element("div", { class: "body" },
          element("div", { class: "name" }, theme.name),
          element("div", { class: "muted" }, [theme.author && `by ${theme.author}`, theme.license, `${Math.round(theme.bytes / 1024)} KB`].filter(Boolean).join(" · ")),
          theme.description ? element("p", {}, theme.description) : "",
          element("div", { class: "row" }, element("a", { class: "btn primary", href: `themes/${theme.file}`, download: `${theme.id}.theme.bin` }, "Download"))))));
    } catch (error) {
      box.replaceChildren(element("p", { class: "muted" }, "The themes are published with each release. There are none to download yet."));
    }
  }
  gallery();
})();
