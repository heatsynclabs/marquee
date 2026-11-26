# Marquee

A 30x8 LED matrix on an ESP8266 that you can talk to over your network.
Give it a hostname, send it text, colors, animations or raw pixels, and it
plays them.

- `http://marquee.local/` for a small web UI
- one JSON command over HTTP, WebSocket, UDP or a CLASP relay
- a control page hosted on GitHub Pages: <https://heatsynclabs.github.io/marquee/>
- twelve built-in effects, a clock, a little robot, and animations you upload yourself
- an idle rotation that cycles through whatever you want it showing
- watches a JSON endpoint and mixes in a message while it is true, which is
  how it shows OPEN HOURS / COME IN when HeatSync Labs is open
- live pixel streaming at 50 fps
- power-limited so a USB supply will not brown out

## What you need

- ESP8266 board (developed on a NodeMCU v3)
- WS2812B / NeoPixel matrix, 30 columns by 8 rows, wired as a serpentine
  starting at the top-right corner
- 5V supply sized for the panel (see [Power](#power))

## Wiring

| Panel | Board |
|-------|-------|
| DIN   | D2 (GPIO4) |
| 5V    | external 5V supply |
| GND   | supply ground **and** board GND |

Power the panel from its own 5V supply, not the board's 3V3 pin. Join the
grounds. A 300-500 ohm resistor in the data line and a 1000 uF capacitor
across the panel's 5V and GND are the usual precautions.

If your panel is wired differently, change the four `MATRIX_*` lines in
[firmware/marquee/config.h](firmware/marquee/config.h).

## Flash it

With PlatformIO:

    pio run -t upload

With arduino-cli:

    arduino-cli core install esp8266:esp8266 \
      --additional-urls http://arduino.esp8266.com/stable/package_esp8266com_index.json
    arduino-cli lib install "Adafruit NeoPixel" ArduinoJson WebSockets
    arduino-cli compile -b esp8266:esp8266:nodemcuv2 firmware/marquee
    arduino-cli upload -b esp8266:esp8266:nodemcuv2 -p /dev/ttyUSB0 firmware/marquee

After the first flash the panel comes up as a Wi-Fi access point called
`marquee-setup`. Join it, open any page, and enter your network details. It
reboots and appears on your network as `marquee.local`.

Later updates go over the air:

    pio run -e ota -t upload                       # or --upload-port <ip>

Always build with PlatformIO from this repository. `platformio.ini` pins the
platform, the libraries, 160 MHz and the flash layout (4 MB, 2 MB filesystem).
A build with a different layout cannot mount the saved filesystem, so the panel
comes back with its settings wiped and opens the setup portal. If you must use
arduino-cli, pass `-b esp8266:esp8266:nodemcuv2:xtal=160,eesz=4M2M`.

## Use it

Anything that speaks HTTP works:

    curl "http://marquee.local/api/cmd?say=hello&color=red"
    curl "http://marquee.local/api/cmd?anim=fire&now=true"
    curl -d '{"text":"party time","color":"rainbow","repeat":3}' \
         http://marquee.local/api/cmd

Or the bundled client:

    tools/marquee.py say "hello world" --color red
    tools/marquee.py anim robot
    tools/marquee.py bright 90
    tools/marquee.py idle clock
    tools/marquee.py upload cat cat.gif --fps 12
    tools/marquee.py play cat
    tools/marquee.py stream cat.gif --loop

Messages queue up and play in order. When the queue drains the panel returns to
its idle rotation.

The full command reference is in [docs/api.md](docs/api.md).

## What it shows when nobody is talking to it

The idle rotation is a short list of things that take turns, fifteen seconds
each by default:

    tools/marquee.py rotate --set clock robot
    tools/marquee.py rotate --set clock '{"text":"HEATSYNC LABS","color":"#ff9f43"}'
    tools/marquee.py rotate --ms 20000

A message always interrupts the rotation and the rotation picks up again
afterwards.

## Lab open status

The panel polls a JSON endpoint every five minutes and, while the value it
watches is true, mixes an extra entry into the rotation every other turn:

    clock -> OPEN HOURS ... -> HEATSYNC LABS -> OPEN HOURS ... -> robot -> ...

The open entry holds OPEN still for two seconds (it fits the panel), then
scrolls the whole sentence once: OPEN HOURS - WE'RE OPEN - COME IN, followed by
UNTIL 9 PM when the feed also says when the lab closes
(`"open_until": "21:00"`). The panel is 30 pixels wide, five letters, so a
short word can stand still and anything longer reads best as one smooth
scroll.

Out of the box it reads `https://members.heatsynclabs.org/space_api.json` and
watches `open`, the same SpaceAPI document the HeatSync Labs website uses for
its own door indicator. When the lab is closed, nothing extra is shown.

    tools/marquee.py status                     what it last saw
    tools/marquee.py rotate --open "OPEN|COME IN - {until}"       change the frames
    tools/marquee.py rotate --open ""                              turn it off

Point it somewhere else with `status_url` and `status_key`; any endpoint
returning a JSON boolean works. Details in [docs/api.md](docs/api.md).

## Animations

Twelve effects ship with the firmware: `robot`, `rainbow`, `plasma`, `fire`, `rain`,
`sparkle`, `comet`, `chase`, `breathe`, `confetti`, `solid`, `off`, plus
`clock`.

Your own animations are uploaded a frame at a time and stored in the panel's
filesystem, so they survive a reboot:

    tools/marquee.py upload wave wave.gif --fps 12
    tools/marquee.py play wave

For live output, push raw frames instead. The panel listens on UDP port 21324
and speaks the same realtime protocol as WLED, so tools like xLights or
Hyperion can drive it directly. `tools/marquee.py stream` uses the same path.

## Control from outside your network

The panel can also be driven through a [CLASP](https://clasp.to) relay, which
gives it an address that works from anywhere:

    tools/clasp_bridge.py --device kitchen --host marquee.local
    tools/marquee.py --relay wss://relay.clasp.to --device kitchen say "on my way"

The bridge holds the connection to the public relay so the ESP8266 does not
have to; there is also a small local relay in `tools/clasp_relay.py` if you
would rather keep everything on your own network. [docs/clasp.md](docs/clasp.md)
covers all three arrangements and why the panel cannot reach
`wss://relay.clasp.to` by itself.

## Control page

<https://heatsynclabs.github.io/marquee/> is the full console (messages,
rotation, status, stored animations, raw commands). It runs in your browser
and talks straight to the panel, so you need to be on the same network as it.

The page is https and the panel is plain http on the local network. Chrome and
Edge allow that after asking for local network access; accept the prompt (or
allow it from the icon at the left of the address bar). Safari and Firefox do
not; in those, open `http://marquee.local/`, which is the panel's own simpler
control page. If `.local` names do not resolve on your device, type the
panel's IP address into the host field instead.

## Staying up

The panel is meant to run unattended for months. What keeps it that way:

- Wi-Fi never blocks. It keeps retrying for as long as the panel is powered.
  The setup portal only opens on a panel with no network saved, or one that has
  not reached its network for 3 minutes since boot, and it closes again on its
  own once the network is back. A panel that loses Wi-Fi for 10 minutes
  restarts.
- Wi-Fi modem sleep is off, so replies and mDNS stay quick.
- The LED driver turns interrupts off for about 7 ms per frame, which Wi-Fi
  feels. Frames are capped at 30 per second and frames that did not change are
  not sent at all.
- No floating point in the effects; the ESP8266 has no FPU.
- The status poll reuses one TLS client, checks the largest free block before
  it starts, has short DNS and connect timeouts, and runs only on a still frame.
- Every value used as a divisor is clamped, so no command can crash the panel.
- WebSocket clients are pinged and dropped when they stop answering.
- If the heap stays too fragmented for a minute, the panel restarts at an idle
  moment rather than limp on. Three quick crashes in a row turn the optional
  relay client off.
- `/api/state` reports `reset_reason`, `max_block`, `heap_frag`,
  `loop_max_ms` and `wifi_drops`, so you can see how it is doing.

## Power

240 pixels at full white would draw about 14 A, far more than any USB port.
The firmware measures what each frame would draw and scales it down to fit a
budget, 800 mA by default. Raise it once you know your supply:

    curl -d '{"power_ma":3000}' http://marquee.local/api/config

Set it to `0` to disable the limiter entirely. `draw_ma` in `/api/state` shows
what the current frame is asking for; when it equals `power_ma` the limiter is
dimming the panel.

## Layout

    firmware/marquee/   the sketch
      config.h          pin, panel size, wiring and defaults
      matrix.*          pixel buffer, mapping, text drawing
      effects.*         built-in animations
      robot.*           the robot routine
      player.*          the queue and the idle rotation
      commands.*        the JSON command interpreter
      web.*             HTTP, WebSocket and UDP
      status.*          the JSON status poll
      clasp_link.*      optional relay client
      net.*             Wi-Fi, setup portal, mDNS, OTA, NTP
      health.*          reset reason, loop timing, heap guard
    tools/              command line client, relay bridge and a local relay
    docs/               API and relay documentation, and the hosted
                        control page (docs/index.html, served by GitHub Pages)

## License

MIT
