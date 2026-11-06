# Install

You install Dekatron by pasting a prompt into an AI assistant that can use the terminal of your computer, such as Claude Code. The assistant checks your computer, finds the clock, copies its flash, asks you what it needs, writes the firmware and checks that the clock answers. You answer its questions and approve what it asks.

::: warning The installer comes with the first release
The prompt, `tools/install.py` and the downloads of the firmware are being written, so there is nothing to paste yet. Watch the [repository](https://github.com/yayitazale/dekatron) for v0.1. This page says how it will go, so you can see what to expect.
:::

## What you need

- An **EleksTube IPS Gen2** clock and its **USB cable** (a data cable, not one that only charges). Nothing has to be opened.
- A computer with Python 3.12 or later. The assistant installs the rest.
- Your Wi-Fi name and password, which you type when asked. They are not saved in any file.

## The three steps

### 1. On the clock

Plug it into the computer with the USB cable and keep its own power supply at hand. If your computer cannot see it, the assistant tells you which driver to install (the clock uses a CH340 or CP210x chip).

### 2. On your computer

Open the assistant, paste the prompt, and answer. It will:

1. Check Python and install `esptool` in a folder of its own.
2. Find the clock and its chip, and **stop if it is not the ESP32 of an IPS Gen2**.
3. **Read the whole flash twice**, compare the two copies and save them outside the project. It does not go on without a copy you can restore, because the original firmware is tied to the address of your clock and cannot be downloaded again. See [Is it safe?](/guide/safety).
4. Ask you which profile you want and which theme, showing the previews of the [gallery](/themes).
5. Download the firmware and check its SHA-256, then write it.
6. Set up the Wi-Fi with you, over USB or by scanning the code that the clock shows.
7. Check that the clock answers, set the timezone, and suggest a password.

### 3. Follow along

When it finishes you have the address of the page of the clock, the place where your copy is, and the command to go back to the original firmware.

## Profiles

| Profile | What it has |
| --- | --- |
| `clock` | Just the clock: Wi-Fi setup and network time |
| `full` | Adds the web page, the API, updates and Home Assistant. The one most people want |

You can also build the firmware yourself with [ESPHome](https://esphome.io): see [CONTRIBUTING.md](https://github.com/yayitazale/dekatron/blob/main/CONTRIBUTING.md).
