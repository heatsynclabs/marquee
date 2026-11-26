# API reference

Everything the panel does is driven by one JSON object. The same object works
over HTTP, WebSocket, UDP and a CLASP relay, so anything you can do from a
browser you can also do from a script, a shell, or the other side of the
internet.

- [Command fields](#command-fields)
- [Colors](#colors)
- [Animations](#animations)
- [What plays when: the queue and the idle rotation](#what-plays-when)
- [Status polling](#status-polling)
- [HTTP](#http)
- [WebSocket](#websocket)
- [UDP](#udp)
- [CLASP relay](#clasp-relay)
- [Settings](#settings)
- [Stored animation format](#stored-animation-format)

## Command fields

| Field | Type | Meaning |
|-------|------|---------|
| `text` (or `say`) | string | Message to display, up to 127 characters. |
| `anim` | string | Built-in effect name. |
| `play` | string | Stored animation name. |
| `cards` | string or array | Frames shown one after another, e.g. `"OPEN\|COME IN"` or `["OPEN","COME IN"]`. A frame that fits (five characters) holds still; a longer one scrolls through once. `{until}` becomes `UNTIL 9 PM`, see [Status polling](#status-polling). |
| `color` | string or number | See [Colors](#colors). |
| `rainbow` | bool | Color the text by column instead of using `color`. |
| `speed` | number | Scroll speed in pixels per second, 1 to 200. Default 30, one pixel per frame; above that the text starts to skip columns. |
| `repeat` | number | How many times to play. `-1` repeats forever. Default 1. |
| `scroll` | bool | `false` centers the text instead of scrolling. Default true. |
| `hold` | number | Milliseconds to hold before moving on. For `cards`, how long a still frame stays up (default 2000). |
| `fps` | number | Override a stored animation's frame rate, up to 60. |
| `now` | bool | Drop the queue and play immediately. Default false, which queues. |
| `clear` | bool | Empty the queue and return to the idle rotation. |
| `skip` | bool | Move on to the next queued item. |
| `brightness` | number | 0-255. |
| `power_ma` | number | Supply budget in milliamps. `0` disables the limiter. |
| `idle` | string, object or array | Replace the idle rotation. See [What plays when](#what-plays-when). |
| `rotate` | object | Replace the whole rotation document, the same body `POST /api/rotate` takes. |
| `rotate_ms` | number | How long each idle entry stays up. |
| `save` | bool | Write settings to flash immediately instead of a second later. |
| `queue` | array | A list of command objects, applied in order. |

A command with no `text`, `cards`, `anim` or `play` only changes settings.

```json
{"text": "HELLO", "color": "rainbow", "repeat": 3}
{"anim": "fire"}
{"anim": "fire", "hold": 10000}
{"queue": [{"text": "one"}, {"text": "two", "color": "#00ff88"}]}
{"brightness": 120, "idle": ["clock", {"text": "HEATSYNC LABS"}]}
```

## Colors

`color` accepts a name, a hex string, a packed number, or `rainbow`:

    "red"  "#ff0044"  "#f04"  "0xff0044"  16711748  "rainbow"

Names: `black`/`off`, `white`, `red`, `green`, `blue`, `yellow`, `orange`,
`cyan`, `magenta`, `pink`, `purple`, `lime`, `amber`, `teal`.

Colors are gamma corrected, then scaled by `brightness`, then scaled again if
the frame would draw more than `power_ma`.

## Animations

Built in: `off`, `solid`, `rainbow`, `plasma`, `fire`, `rain`, `sparkle`,
`comet`, `chase`, `breathe`, `confetti`, `clock`, `robot`.

`robot` is a small character routine: an 8 pixel tall robot walks in, waves,
hops and walks off, then flies across on a jetpack trailing rainbow stripes
(the trans flag on about one appearance in three), then walks again. One full routine is about 23.3 s, so give it
`"hold": 23500` in the rotation to let it finish.

`solid`, `sparkle`, `comet`, `chase` and `breathe` use `color`. `clock` reads
the time over NTP and shows `HH:MM`, which fills the panel exactly.

Your own animations are uploaded frame by frame, stored in the panel's
filesystem, and played by name with `play`.

## What plays when

There are three layers:

1. **Realtime.** A UDP packet or a binary WebSocket frame takes the panel over
   for a couple of seconds. Nothing else draws while that is happening.
2. **The queue.** Up to 8 items, played in order. This is where messages go.
3. **The idle rotation.** What shows when the queue is empty.

The rotation is a list of up to 6 items that take turns, each for `ms`
milliseconds or until it finishes naturally, whichever comes first. A message
always interrupts the rotation, and the rotation resumes when the queue drains.

    {"idle": "clock"}                       one effect
    {"idle": {"text": "HEATSYNC LABS"}}     one message, on repeat
    {"idle": ["clock", "plasma"]}           two effects, taking turns
    {"idle": [{"anim": "clock"},
              {"text": "HEATSYNC LABS", "color": "#ff9f43"},
              {"anim": "plasma"}]}

`{"anim": "fire"}` with no `hold` is shorthand for a one-entry rotation, so
sending an effect never blocks messages behind it. With a `hold` it becomes an
ordinary timed queue entry instead.

The rotation is saved to `/rotate.json` and survives reboots.

## Status polling

The panel can watch a JSON endpoint for one boolean and mix an extra entry into
the rotation while it is true. Out of the box it watches whether HeatSync Labs
is open, using the same SpaceAPI document the lab's own website reads.

    GET https://members.heatsynclabs.org/space_api.json  ->  {... "open": true}

While `open` is true, the `open` item appears every `status_every` turns of the
rotation, so it keeps coming back around between everything else. By default it
holds OPEN still, then scrolls the sentence once:

    OPEN  (still, 2 s)  ->  OPEN HOURS - WE'RE OPEN - COME IN - UNTIL 9 PM  (scrolls)

    clock -> (open frames) -> HEATSYNC LABS -> (open frames) -> robot -> ...

`UNTIL 9 PM` comes from `{until}` and only appears when the feed also carries
a closing time as `"open_until": "21:00"` (local, 24 hour). Without it, or once
that time has passed, it is dropped along with the separator before it.

While it is false, the rotation runs normally and nothing extra is shown.

Point it at anything that returns a JSON boolean:

    curl -d '{"status_url":"https://example.com/thing.json","status_key":"ready"}' \
         http://marquee.local/api/config

The poll takes about half a second (the TLS handshake) and only runs while the
panel shows something still: the clock, a solid fill or a held frame. It never
interrupts a message. If nothing still comes along for two minutes it runs at
the start of the next idle entry. A failed poll is retried after 30 s, then
60 s, 2 min and so on up to the normal interval, and a value nobody has
confirmed for 30 minutes is treated as unknown, so a dead network cannot leave
OPEN on the panel all night.
Nested keys are matched by name anywhere in the document, and string values are
skipped, which is how the real `"open": true` is told apart from the
`"icon": {"open": "...png"}` field in the same file.

| Setting | Default | Meaning |
|---------|---------|---------|
| `status_enabled` | true | Poll at all |
| `status_url` | the SpaceAPI URL above | Any http or https URL |
| `status_key` | `open` | JSON key to watch |
| `status_poll_s` | 300 | Seconds between polls |
| `status_every` | 2 | Show the open item every Nth idle turn |

`POST /api/status/poll` forces a poll right away.

Something else can also tell the panel instead of waiting for a poll:

    curl -d '{"open":true,"open_until":"21:00"}' http://marquee.local/api/status

While pushes keep arriving (at least one per two poll intervals) the panel does
not poll at all.

## HTTP

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Web UI |
| GET/POST | `/api/cmd` | Run a command. POST a JSON body, or pass query parameters. |
| GET | `/api/state` | Status, current item, rotation, effects, stored animations |
| GET | `/api/config` | Current settings |
| POST | `/api/config` | Change settings. Add `"reboot": true` to restart. |
| GET | `/api/rotate` | The idle rotation document |
| POST | `/api/rotate` | Replace it: `{"ms":15000,"items":[...],"open":{...}}` |
| POST | `/api/status/poll` | Poll the status endpoint now |
| POST | `/api/status` | Push the value: `{"open":true,"open_until":"21:00"}` |
| GET | `/api/anim` | List stored animations |
| POST | `/api/anim?name=X&fps=N` | Start a new stored animation |
| POST | `/api/anim/frame?name=X` | Append one frame, body in base64 or hex |
| DELETE | `/api/anim?name=X` | Delete one |
| POST | `/api/wifi` | Setup portal form: `ssid`, `pass`, `host`. Saves and restarts. |
| POST | `/api/reboot` | Restart |

Query parameters mirror the JSON fields, so a browser address bar works:

    http://marquee.local/api/cmd?say=hello&color=%23ff0044&repeat=2

`POST /api/rotate` with `"open": false` turns the status entry off.

Every reply carries `Access-Control-Allow-Origin: *`, and `OPTIONS` preflights
answer with `Access-Control-Allow-Private-Network: true`, so a page served from
elsewhere (the [hosted console](https://heatsynclabs.github.io/marquee/)) can
drive the panel.

`/api/state` also reports how the panel is doing:

| Field | Meaning |
|-------|---------|
| `fw` | Firmware version |
| `reset_reason` | Why it last booted (`Power On`, `Software/System restart`, `Exception`, `Hardware Watchdog` ...) |
| `free_heap`, `max_block`, `heap_frag` | Free memory, the largest piece of it, and fragmentation in percent |
| `loop_max_ms` | Longest single pass of the main loop in the last minute. A status poll shows up here as about 500. |
| `wifi_drops` | Times Wi-Fi dropped since boot |
| `led_pushes` | Frames actually sent to the LEDs (unchanged frames are skipped) |
| `time`, `time_synced`, `tz` | Local time as the clock sees it |

## WebSocket

Port 81. Send the same JSON as a text message; errors come back as
`{"ok":false,"error":"..."}`.

A binary message of exactly 720 bytes (30 x 8 x RGB, row major from the top
left) is drawn straight to the panel and holds it for two seconds. Keep sending
frames for live output.

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

    printf '{"say":"from udp"}' | nc -u -w0 marquee.local 21324

## CLASP relay

Turn it on with `{"clasp_enabled":true}` in `/api/config`. The panel subscribes
to `/marquee/<device>/**` on the relay, where `<device>` is `clasp_device` in
the settings. See [clasp.md](clasp.md) for setup and trade-offs.

| Address | Type | Effect |
|---------|------|--------|
| `/marquee/<device>/cmd` | string | Any command object from this page, as JSON |
| `/marquee/<device>/say` | string | Queue a message |
| `/marquee/<device>/text` | string | Same as `say` |
| `/marquee/<device>/anim` | string | Run an effect immediately |
| `/marquee/<device>/brightness` | int, float or bool | Set brightness |
| `/marquee/<device>/status` | string | Published by the panel: `online`, every 30s |

`cmd` is the general one, and reaches every field in the table at the top:

    /marquee/kitchen/cmd  =  {"text":"dinner","color":"amber","repeat":2}
    /marquee/kitchen/cmd  =  {"idle":["clock","fire"],"rotate_ms":20000}
    /marquee/kitchen/cmd  =  {"rotate":{"open":{"text":"OPEN","color":"lime"}}}

Values arrive as CLASP parameters (`SET`). Commands must fit in a single frame
of about 1 KB.

## Settings

`GET /api/config` returns them; `POST /api/config` changes any subset.

| Key | Default | Meaning |
|-----|---------|---------|
| `host` | `marquee` | mDNS and DHCP hostname |
| `ssid`, `pass` | from the setup portal | Wi-Fi credentials, `pass` is never returned |
| `brightness` | 60 | 0-255 |
| `power_ma` | 800 | Supply budget, `0` disables the limiter |
| `tz` | `MST7` | POSIX timezone for the clock. Arizona does not observe daylight saving; elsewhere use e.g. `PST8PDT,M3.2.0,M11.1.0`. Applies immediately. |
| `status_*` | see above | Status polling |
| `clasp_enabled` | false | Connect to a CLASP relay |
| `clasp_url` | `wss://relay.clasp.to` | Relay URL, `ws://` or `wss://` |
| `clasp_device` | `esp` + chip id | Address namespace on the relay |
| `reboot` | - | Pass `true` to restart after saving |

## Stored animation format

Each is a file in the panel's filesystem under `/anim/`:

    "MQA1"  4 bytes
    width   uint16 big-endian
    height  uint16 big-endian
    fps     uint16 big-endian
    frames  width * height * 3 bytes each, row major, top-left first

Upload one frame at a time; the frame count comes from the file size.
`tools/marquee.py upload` does this for you.
