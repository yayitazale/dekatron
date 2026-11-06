# Dekatron

Open firmware for the **EleksTube IPS Gen2** clock, the one with six small IPS screens. It is built on [ESPHome](https://esphome.io) and gives the clock:

- a **web page** in the clock itself, with its controls, a backup and a place to upload files;
- **themes**: animations that cross the six tubes, with lights that follow the picture;
- **clock faces** made from a font or from your own pictures;
- updates over the air, and Home Assistant if you want it.

Dekatron is compatible with the EleksTube IPS clocks and has no affiliation with EleksTube.

> The project is being prepared for its first public release. Installation, themes and a theme creator will live on the project's website, which is built from `site/`.

## What is here

| Folder | What it holds |
|---|---|
| `components/` | The ESPHome components: the tubes (`elekstube_ips`) and the HTTP API (`dekatron_api`) |
| `firmware/` | The profiles (`clock.yaml`, `full.yaml`), their packages and the board file with the pins |
| `web/` | The page that the clock serves |
| `site/` | The static site and the theme creator |
| `tools/` | Python tools that build themes, faces and the store file the clock takes |
| `docs/` | [`api.md`](docs/api.md) and [`web.md`](docs/web.md) |
| `tests/` | Tests of the tools and of the page libraries |

## Before you flash anything

The original firmware is tied to the clock's MAC address and cannot be downloaded again. **Make a copy of the whole flash first** and check that you can read it back. Only the EleksTube IPS Gen2 has been tried; other similar boards are welcome as a new file in `firmware/boards/`.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md). The code is under the [GNU GPL v3 or later](LICENSE), the themes and faces under [CC BY-SA 4.0](themes/LICENSE), and third-party parts are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
