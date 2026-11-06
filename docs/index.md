---
layout: home

hero:
  name: Dekatron
  text: Six little screens, your own clock.
  tagline: Open firmware for the EleksTube IPS Gen2 clock. A web page in the clock, themes that cross the six tubes, faces from any font, and updates over the air. You install it by pasting a prompt into an AI assistant.
  image:
    src: /logo.svg
    alt: Dekatron
  actions:
    - theme: brand
      text: Set up my clock
      link: /guide/getting-started
    - theme: alt
      text: Themes
      link: /themes
    - theme: alt
      text: Make a theme
      link: /creator/index.html
      target: _self

features:
  - icon: 🌐
    title: A page in the clock
    details: Open the clock in a browser on your phone. Brightness, lights, night mode, faces and a countdown, and one place to upload files and keep a backup.
  - icon: 🎞️
    title: Themes across six tubes
    details: Four animations (startup, connecting, connected, end of a countdown) that cross all six screens, with lights that take the color of the picture.
  - icon: 🔤
    title: Faces from any font
    details: Pick a free font or bring your own, choose the color and the glow, and the creator makes the ten digits. Up to five faces on the clock.
  - icon: 🤖
    title: Installed by an AI
    details: Paste one prompt into an assistant with a terminal. It copies your flash first, asks what it needs, writes the firmware and checks that the clock answers.
  - icon: 🛟
    title: Made to be undone
    details: The original firmware is copied and verified before anything is written, a firmware that does not start is dropped, and one command brings the old clock back.
  - icon: 🧩
    title: Open files, open code
    details: One documented file format, a checker that says what is wrong, and tools for a person or an assistant to make themes. GPL code, CC BY-SA themes.
---

::: warning Work in progress
The first public release is not out yet. The firmware, the page of the clock, the file format and the creator are here and work. The installer and the downloads of the firmware arrive with v0.1, so there is nothing to paste yet.
:::

## Does it work on my clock?

| Clock | Status |
| --- | --- |
| **EleksTube IPS Gen2** (also sold as G2.1): six 135 × 240 IPS screens, ESP32 PICO-D4 with 4 MB of flash, six RGB lights, DS3231 clock, four buttons | ✅ The one it is made for |
| Other clocks with the same parts on other pins | 🧪 Not tried. Copy [`firmware/boards/elekstube-ips-gen2.yaml`](https://github.com/yayitazale/dekatron/blob/main/firmware/boards/elekstube-ips-gen2.yaml), change the pins, and send it as a pull request |
| The first EleksTube (tubes or OLED), the Gen1 IPS without Wi-Fi | ❌ Not supported |

## How it works

1. **Install** with an AI assistant, or from the browser. It copies the original flash first. [Install →](/guide/getting-started)
2. **Use** the page of the clock for the controls, and **Files** to upload what you make. [Using the clock →](/guide/using)
3. **Make** themes and faces in the [creator](/creator/index.html){target="_self"}, from the [command line](/making-bins), or by asking an [AI](/create/ai). Upload the file to the clock.
4. **Update** from the same page: it tells you when there is a newer firmware. [Updating →](/guide/updating)
