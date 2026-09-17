# Marquee

A 30x8 LED matrix on an ESP8266 that you can talk to over your network.
Give it a hostname, send it text, colors, animations or raw pixels, and it
plays them.

- `http://marquee.local/` for a small web UI
- one JSON command over HTTP, WebSocket, UDP or a CLASP relay
- eleven built-in effects, a clock, and animations you upload yourself
- an idle rotation that cycles through whatever you want it showing
- watches a JSON endpoint and mixes in a message while it is true, which is
  how it shows LAB OPEN when HeatSync Labs is open
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

Later updates can go over the air:

    OTA=$(find ~/Library/Arduino15/packages/esp8266 -name espota.py | head -1)
    python3 "$OTA" -i marquee.local -f /path/to/marquee.ino.bin

Building with `-b esp8266:esp8266:nodemcuv2:xtal=160` runs the board at 160 MHz,
which halves the pause during a status poll. Everything else works the same.

## Use it

Anything that speaks HTTP works:

    curl "http://marquee.local/api/cmd?say=hello&color=red"
    curl "http://marquee.local/api/cmd?anim=fire&now=true"
    curl -d '{"text":"party time","color":"rainbow","repeat":3}' \
         http://marquee.local/api/cmd

Or the bundled client:

    tools/marquee.py say "hello world" --color red
    tools/marquee.py anim plasma
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

    tools/marquee.py rotate --set clock plasma
    tools/marquee.py rotate --set clock '{"text":"HEATSYNC LABS","color":"#ff9f43"}'
    tools/marquee.py rotate --ms 20000

A message always interrupts the rotation and the rotation picks up again
afterwards.

## Lab open status

The panel polls a JSON endpoint every five minutes and, while the value it
watches is true, mixes an extra entry into the rotation every other turn:

    clock -> LAB OPEN -> HEATSYNC LABS -> LAB OPEN -> plasma -> LAB OPEN -> ...

Out of the box it reads `https://members.heatsynclabs.org/space_api.json` and
watches `open`, the same SpaceAPI document the HeatSync Labs website uses for
its own door indicator. When the lab is closed, nothing extra is shown.

    tools/marquee.py status                     what it last saw
    tools/marquee.py rotate --open "LAB OPEN"   change the message
    tools/marquee.py rotate --open ""           turn it off

Point it somewhere else with `status_url` and `status_key`; any endpoint
returning a JSON boolean works. Details in [docs/api.md](docs/api.md).

## Animations

Eleven effects ship with the firmware: `rainbow`, `plasma`, `fire`, `rain`,
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
      player.*          the queue and the idle rotation
      commands.*        the JSON command interpreter
      web.*             HTTP, WebSocket and UDP
      status.*          the JSON status poll
      clasp_link.*      optional relay client
      net.*             Wi-Fi, setup portal, mDNS, OTA
    tools/              command line client, relay bridge and a local relay
    docs/               API and relay documentation

## License

MIT
