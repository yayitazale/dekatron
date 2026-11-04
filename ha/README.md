# Home Assistant

These scripts live in Home Assistant, so this page only documents how to use them.

| Script | Purpose |
|---|---|
| `script.elekstube_play_animation` | Plays an effect, with optional color, tubes and priority |
| `script.elekstube_show_text` | Shows text or a Home Assistant value |
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
| `state` | from the clock | `clock`, `test` or `matrix 3/10 s` |

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

Invalid and rejected requests are only reported in the device log.
