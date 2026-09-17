# Marquee

A 30x8 LED matrix on an ESP8266 that you can talk to over your network.
Give it a hostname, send it text, colors, animations or raw pixels, and it
plays them.

- `http://marquee.local/` for a small web UI
- one JSON command over HTTP, WebSocket, UDP or a CLASP relay
- eleven built-in effects, a clock, and animations you upload yourself
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

    arduino-cli upload -b esp8266:esp8266:nodemcuv2 -p marquee.local firmware/marquee

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

Messages queue up and play in order. When the queue drains the panel returns
to its idle effect, which is the clock out of the box.

The full command reference is in [docs/api.md](docs/api.md).

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
gives it an address that works from anywhere. See [docs/clasp.md](docs/clasp.md)
for the two ways to set that up and the trade-offs between them.

## Power

240 pixels at full white would draw about 14 A, far more than any USB port.
The firmware measures what each frame would draw and scales it down to fit a
budget, 800 mA by default. Raise it once you know your supply:

    curl -d '{"power_ma":3000}' http://marquee.local/api/config

Set it to `0` to disable the limiter entirely.

## Layout

    firmware/marquee/   the sketch
      config.h          pin, panel size and wiring
      matrix.*          pixel buffer, mapping, text drawing
      effects.*         built-in animations
      player.*          the queue and playback
      commands.*        the JSON command interpreter
      web.*             HTTP, WebSocket and UDP
      clasp_link.*      optional relay client
      net.*             Wi-Fi, setup portal, mDNS, OTA
    tools/              command line client and relay bridge
    docs/               API and relay documentation

## License

MIT
