# API

Everything the panel does is driven by one JSON object. The same object works
over HTTP, WebSocket, UDP and the CLASP relay.

## Command fields

| Field | Type | Meaning |
|-------|------|---------|
| `text` (or `say`) | string | Message to display. Up to 127 characters. |
| `anim` | string | Built-in effect name. |
| `play` | string | Stored animation name. |
| `color` | string | `red`, `#ff0044`, `#f04`, or `rainbow`. |
| `speed` | number | Scroll speed in pixels per second. Default 45. |
| `repeat` | number | How many times to play. `-1` repeats forever. Default 1. |
| `scroll` | bool | `false` centers the text instead of scrolling. Default true. |
| `hold` | number | Milliseconds to hold before moving on. Effects default to forever. |
| `fps` | number | Override a stored animation's frame rate. |
| `now` | bool | Drop the queue and play immediately. Default false, which queues. |
| `clear` | bool | Empty the queue and return to idle. |
| `skip` | bool | Move on to the next queued item. |
| `brightness` | number | 0-255. |
| `power_ma` | number | Supply budget in milliamps. `0` disables the limiter. |
| `idle` | string | Effect to show when the queue is empty. |
| `save` | bool | Write settings to flash immediately instead of a second later. |
| `queue` | array | A list of command objects, applied in order. |

A command with no `text`, `anim` or `play` only changes settings.

Built-in effects: `off`, `solid`, `rainbow`, `plasma`, `fire`, `rain`,
`sparkle`, `comet`, `chase`, `breathe`, `confetti`, `clock`.

### Examples

```json
{"text": "HELLO", "color": "rainbow", "repeat": 3}
{"anim": "fire", "hold": 10000}
{"queue": [{"text": "one"}, {"text": "two", "color": "#00ff88"}]}
{"brightness": 120, "idle": "plasma"}
```

## HTTP

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web UI |
| GET/POST | `/api/cmd` | Run a command. POST a JSON body, or pass query parameters. |
| GET | `/api/state` | Status, effect list, stored animations |
| GET | `/api/config` | Current settings |
| POST | `/api/config` | Change settings. Add `"reboot": true` to restart. |
| GET | `/api/anim` | List stored animations |
| POST | `/api/anim?name=X&fps=N` | Start a new stored animation |
| POST | `/api/anim/frame?name=X` | Append one frame, body in base64 or hex |
| DELETE | `/api/anim?name=X` | Delete one |
| POST | `/api/reboot` | Restart |

Query parameters mirror the JSON fields, so a browser address bar works:

    http://marquee.local/api/cmd?say=hello&color=%23ff0044&repeat=2

## WebSocket

Port 81. Send the same JSON as a text message.

A binary message of exactly 720 bytes (30 x 8 x RGB) is drawn straight to the
panel and holds it for two seconds. Keep sending frames for live output.

```js
const ws = new WebSocket('ws://marquee.local:81/');
ws.send(JSON.stringify({text: 'hi', color: 'lime'}));
ws.send(new Uint8Array(720));
```

## UDP

Port 21324, the same port and protocols WLED uses, so existing realtime tools
work unchanged.

| First byte | Protocol | Payload |
|-----------|----------|---------|
| 1 | WARLS | repeating index, R, G, B |
| 2 | DRGB | RGB for every pixel from zero |
| 4 | DNRGB | 16-bit start index, then RGB |

The second byte is how many seconds the panel stays under external control
after the last packet. A packet starting with `{` is treated as a JSON command
instead.

```sh
printf '{"say":"from udp"}' | nc -u -w0 marquee.local 21324
```

## Stored animations

Each is a file in the panel's filesystem:

    "MQA1"  4 bytes
    width   uint16 big-endian
    height  uint16 big-endian
    fps     uint16 big-endian
    frames  width * height * 3 bytes each, row major, top-left first

Upload one frame at a time; the frame count comes from the file size.
`tools/marquee.py upload` does this for you.
