# Using the clock

## The page of the clock

Open the address of the clock in a browser, on a phone or a computer on the same network. The page lives in the clock itself and has three tabs.

- **Clock:** displays (on and brightness), lights (color, brightness, effect), the display (12 or 24 hours, digit change, night mode), the face that is shown, a countdown and a few effects.
- **Files:** upload a theme, a face, a backup or a firmware, see what is on the clock, and keep a backup.
- **Settings:** timezone, password, and the firmware with its updates.

The details are in [The page of the clock](/web).

## Uploading files

The page takes any `.bin` file for the clock and says **what it is, who made it and under which license before it changes anything**. A theme replaces the theme and keeps your faces; a face goes into a free slot or replaces one; a backup brings everything back; a firmware updates the clock and needs a password. Every dialog offers to download a backup first.

The site cannot send anything to the clock: this site is served over HTTPS and the clock speaks plain HTTP, and a browser does not allow that. So the way is always **make or download the file here, then upload it on the page of the clock**.

## Wi-Fi

A clock without Wi-Fi shows a code on its screens. Scan it with your phone, choose your network, and the clock joins it. If it loses the network it keeps the time from its own clock.

## Buttons

| Button | What it does |
| --- | --- |
| **Left**, **right** | Change the face |
| **Mode** | Starts a countdown |
| **Power** | Turns the screens off and on |
| **Mode**, held for 10 seconds | Forgets the Wi-Fi |

## Home Assistant

The `full` profile has the native API of ESPHome, so the clock can be adopted in the ESPHome dashboard and shows up in Home Assistant, with its lights, brightness and night mode. Animations can be started from Home Assistant with the same JSON as the [HTTP API](/api).
