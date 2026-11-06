# Updating

The clock never goes to the internet. Its page does. When you open it, and at most once every two hours, the page asks this site for `versions.json`, which each release publishes, and compares it with the version of the clock. If there is a newer one, **Settings** shows a dot and a notice with what changed and an **Update** button.

**Update** then:

1. downloads the firmware from this site,
2. checks its SHA-256 against the one in `versions.json`,
3. sends it to the clock, which checks it again, restarts, and **goes back to the old firmware if the new one does not start**.

An update needs a **password** on the clock, because nobody should be able to send a program to it without one. Without internet on the device you browse from, the notice does not appear; the clock keeps working, and you can always download a `.ota.bin` from the [releases](https://github.com/yayitazale/dekatron/releases) and upload it in **Files**.

The page sends nothing about you or the clock when it asks: it only reads a static file.

## For people who use ESPHome

If you build your own firmware, update it as any ESPHome device, from the dashboard or with `esphome run`. The version of Dekatron is in `version` of `/api/state`.
