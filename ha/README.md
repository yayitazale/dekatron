# Home Assistant

These scripts live in Home Assistant, so this page only documents how to use them.

| Script | Purpose |
|---|---|
| `script.elekstube_play_animation` | Plays an effect, with optional color, tubes and priority |
| `script.elekstube_show_text` | Shows text or a Home Assistant value |
| `script.elekstube_play_clip` | Plays a clip from the store |
| `script.elekstube_notify` | Urgent pulse (priority 1) in the chosen color |
| `script.elekstube_stop_animation` | Stops the animation and returns to the time |

```yaml
- action: script.elekstube_notify
  data:
    color: "#00ff00"
    duration: 5

- action: script.elekstube_show_text
  data:
    text: "{{ states('sensor.outdoor_temperature') }}°"
    duration: 15
```

## MQTT API

Topic prefix: `elekstube/elekstube/animation`.

| Topic | Direction | Payload |
|---|---|---|
| `play` | to the clock | JSON request, see below |
| `stop` | to the clock | anything |

The state is published to `elekstube/elekstube/text_sensor/animation_state/state` as `clock`, `test` or `matrix 3/10 s`.

```json
{"type": "effect", "name": "matrix", "duration": 10, "color": "#00ff46", "tubes": "all", "priority": 2}
```

Only `name` is required.

- `name`: `matrix`, `glitch`, `sweep`, `pulse` or `countdown`.
- `duration`: seconds, from 1 to 3600. Defaults to 10.
- `color`: `#rrggbb`. Defaults to the color of the effect. `countdown` draws with the clock face and ignores it on the tubes.
- `tubes`: `all`, or tube numbers from 1 (seconds units, rightmost) to 6, such as `12`. The other tubes keep the time.
- `priority`: 1 (most urgent) to 9. Defaults to 2. A request replaces a running animation of equal or lower urgency and is rejected otherwise.

Text uses `"type": "text"` instead of a name, with the other fields working as above:

```json
{"type": "text", "text": "{warning} Puerta abierta", "duration": 15, "color": "#ff2800", "scroll": false}
```

- `text`: required, up to 64 characters. ASCII plus `ÁÉÍÓÚÜÑáéíóúüñ¿¡°€`. Anything else shows as `?`.
- Icons: `{warning}`, `{bell}`, `{drop}`, `{sun}`, `{heart}` and `{lock}`.
- `scroll`: text that does not fit one character per tube always scrolls. This forces scrolling for shorter text.
- `color`: defaults to white and leaves the LEDs alone. A color is also applied to the LEDs.

Clips use `"type": "clip"`:

```json
{"type": "clip", "name": "alarm", "loop": 3}
```

- `name`: required, a clip in the store.
- `loop`: how many times to play it. Without it, the clip repeats for `duration` seconds.
- `color`: optional. Only colors the LEDs, which are otherwise left alone.

Invalid and rejected requests are only reported in the device log.

## Store

The clock keeps one store with its extra clock faces and every clip, up to 1152 KB. Build it and publish its URL:

```bash
python tools/make_clips.py store.bin --face Marble=faces/marble --clip alarm=alarm.gif --clip wave=wave.gif:panorama --fps 15
```

```json
{"url": "http://homeassistant.local:8123/local/store.bin"}
```

Publish to `elekstube/elekstube/store/download` and follow the Store sensor, which says how many faces and clips there are, or how a download is going. The same store can be uploaded to the clock's own [HTTP API](../docs/api.md).

- A file in `/config/www` is served at `/local/`. Only plain HTTP is supported.
- A download replaces the whole store. A wrong URL or a file that is not a store keeps the current one, but a download that fails after it starts leaves the store empty until the next one succeeds. The faces built into the firmware, Nixie and Ice, are always there.
- The Clock Face selector lists the faces of the store, but Home Assistant only reads its options when it connects, so reload the integration to see new ones.
- Clips play the same picture on every tube at up to about 20 fps. Panoramas show one picture across all six at only 3 to 4 fps. Animations that cross the tubes are made as scenes, which reach 15 to 20 fps, see `tools/make_scenes.py`.
- Videos need `ffmpeg` on the PATH. GIF and animated WebP do not.
