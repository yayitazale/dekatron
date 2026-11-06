# HTTP API

The clock serves a small API on port 80, from the web server that ESPHome already runs. It needs the `web` package, which `firmware/full.yaml` includes.

Everything lives under `/api/` and answers JSON, except the download of the store.

## Password and safety

By default there is no password. Setting one with `POST /api/password` makes every call but `GET /api/info` ask for it, as HTTP basic authentication. The user name is ignored.

Calls that change something are `POST`s, and they must also carry the header `X-Dekatron` with any value. A web page of another site cannot add that header to a request it makes to the clock without a permission that this API never gives, so it cannot make your browser change the clock behind your back.

The clock speaks plain HTTP, so on a network you do not trust a password can be seen. Keep it on your home network.

## Calls

| Call | Purpose |
|---|---|
| `GET /` | The [web page](web.md) of the clock. Public |
| `GET /api/info` | Who this is. Public: `product`, `api`, `name`, `auth` |
| `GET /api/state` | State, settings, the faces and clips it has, the name of its theme, memory and store status |
| `GET /api/store` | The store as it is in flash, a file of up to 1152 KB |
| `POST /api/store` | Replaces the store with the body, as `application/octet-stream` |
| `POST /api/play` | Starts an animation, with the JSON of the [MQTT play topic](../ha/README.md) |
| `POST /api/stop` | Stops it |
| `POST /api/settings` | Changes settings, see below |
| `POST /api/controls` | Changes what the clock shows, see below |
| `POST /api/password` | `{"password": "..."}` sets it, and an empty one removes it |

Errors answer `{"error": "..."}` with a status of 400 (a missing header or empty request), 401 (password), 404, 409 (another store is being written) or 422 (the content is not valid).

## Settings

`POST /api/settings` takes any of these in a JSON object. The answer says what the clock understood.

- `timezone`: a POSIX timezone such as `CET-1CEST,M3.5.0,M10.5.0/3`. The clock has no table of named zones, so a client translates `Europe/Madrid` into its POSIX form first.
- `boot_animation`: `false` skips the astronaut that plays when the clock starts.
- `face`: the name of a clock face, from `faces` in `/api/state`.

Settings are kept across restarts.

## Controls

`GET /api/state` has a `controls` object, and `POST /api/controls` changes any part of it with the same shape. What the profile does not have is left out. A call that changes nothing valid answers 422 and changes nothing.

```json
{"displays": true, "brightness": 100,
 "night": {"on": false, "start": 23, "end": 7, "brightness": 20},
 "hour24": true, "leading_zero": true, "transition": "Off",
 "leds": {"on": false, "color": "#ff0000", "brightness": 50, "effect": "None", "effects": ["None", "Pulse", "Rainbow"]}}
```

- `brightness`: 5 to 100. `night.brightness`: 0 to 100. `night.start` and `night.end`: hours, 0 to 23.
- `transition`: `Off` or `Fade`.
- `leds.color`: `#rrggbb`. `leds.brightness`: 1 to 100. `leds.effect`: one of `leds.effects`.
- Turning the lights off (`"leds": {"on": false}`) does only that, and the rest of the same call is ignored.

Clips with lights say `"leds": true` in `clips` of the state.

## Replacing the store

The store is one file made by `tools/make_clips.py` (see its help). It holds up to four clock faces, and a **theme**: its name and data, and the four animations `boot`, `connecting`, `online` and `countdown_end` that the clock always needs. A new store replaces the whole previous one. A file with only a theme, such as the one `tools/make_scenes.py` writes, is what the page takes for Change theme, keeping the faces.

```bash
curl -X POST http://elekstube.local/api/store -H "X-Dekatron: 1" \
     -H "Content-Type: application/octet-stream" --data-binary @store.bin
```

What to expect:

- **It takes about 9 s** for the full 840 KB of the default theme, of which the flash is busy for 4.5 s. The tubes keep drawing, at about a sixth of their speed, and recover when it ends.
- **A file that is not a store** is refused before anything is erased, and the current store stays.
- **A file that is damaged further in**, which only its checksum reveals at the end, is refused too, but by then the old store is gone. Check the file before sending it.
- **A connection that breaks halfway** leaves the store empty. The clock gives up on it after 10 s, and a new upload then works.
- The face built into the firmware, Nixie, is always available. The store adds up to four more, so a clock has five faces at most.
