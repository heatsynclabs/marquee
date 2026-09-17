# Driving the panel from outside your network

`marquee.local` only works on your own network. To reach the panel from
anywhere, put a [CLASP](https://clasp.to) relay between you and it. CLASP is a
compact binary pub/sub protocol; a relay is a server that clients connect to
and exchange addressed values through.

There are three ways to set this up. Start with the bridge.

## Option 1: a bridge on your network (recommended)

A short script holds the connection to the public relay and forwards what it
receives to the panel over HTTP. Run it on anything that is always on and can
see the panel, such as a Raspberry Pi or a home server.

    tools/clasp_bridge.py --device kitchen --host marquee.local

Then, from anywhere:

    tools/marquee.py --relay wss://relay.clasp.to --device kitchen say "on my way"

The panel never talks to the relay, so there is no TLS and no memory pressure
on it, and it keeps working normally if the bridge or the relay goes away.

## Option 2: your own relay, on your own network

A plain `ws://` relay costs the panel almost nothing, and your messages stay on
your network. There is a small one in this repository:

    tools/clasp_relay.py                # listens on 0.0.0.0:7330

Point the panel at it:

    curl -d '{"clasp_enabled":true,"clasp_url":"ws://192.168.1.10:7330","reboot":true}' \
         http://marquee.local/api/config

    tools/marquee.py --relay ws://192.168.1.10:7330 --device esp1a2b3c say hi

For the full implementation, with persistence, authentication and federation,
build the real one:

    git clone https://github.com/lumencanvas/clasp
    cd clasp/deploy/relay && cargo run --release

Both listen on port 7330.

## Option 3: the panel connects to the public relay itself

This does not fit on an ESP8266, and the firmware will tell you so rather than
trying. The details are worth knowing, because they are the reason the bridge
exists.

- `relay.clasp.to` is served over TLS behind a CDN that **will not negotiate a
  smaller maximum record size**, so BearSSL needs a full 16 KB receive buffer.
  With a smaller one the handshake dies with `Incoming record is too large`.
- 16 KB of buffer plus the handshake's working memory needs roughly 30 KB of
  heap. With the web server, the WebSocket server and the panel running there
  is about 29 KB free, so the firmware logs how much it is short by and carries
  on without the relay. You can see it in `clasp.last` in `/api/state`.
- **The public relay greets every client with its entire state**, which was
  around 53 KB when this was written and grows over time. This is why the
  firmware carries its own small WebSocket client instead of using a library:
  the usual Arduino one drops the connection on any message over 15 KB, which
  would leave the panel in a reconnect loop. This client reads past oversized
  frames and discards them a chunk at a time, so it never buffers them.
- **The public relay is a shared, unauthenticated namespace.** Anyone who knows
  your device id can write to it, and anyone watching can see what you send.
  Pick an unguessable device id, or run your own relay.
- Certificates are not verified. Traffic is encrypted, but a relay that is not
  the one you meant would not be detected.

If you strip the firmware down (dropping the WebSocket server and the status
poll frees the most), raise nothing and it will simply start working: the
guard in `config.h` only refuses while the heap is genuinely too small. Should
it ever crash on startup anyway, the panel counts its own restarts and turns
the relay off after three, so a bad relay setting cannot leave it stuck.

## Addresses

The panel subscribes to `/marquee/<device>/**`, where `<device>` is
`clasp_device` in the settings and defaults to `esp` plus the chip id.

| Address | Value | Effect |
|---------|-------|--------|
| `/marquee/<device>/cmd` | JSON string | Any command from [api.md](api.md) |
| `/marquee/<device>/say` | string | Queue a message |
| `/marquee/<device>/text` | string | Same as `say` |
| `/marquee/<device>/anim` | string | Run an effect immediately |
| `/marquee/<device>/brightness` | int, float or bool | Set brightness |

It publishes `/marquee/<device>/status` as `online` when it connects and every
30 seconds after that.

`cmd` carries the whole API, so anything on the [api.md](api.md) page is
reachable from the relay:

    /marquee/kitchen/cmd  =  {"text":"dinner is ready","color":"amber"}
    /marquee/kitchen/cmd  =  {"idle":["clock","plasma"],"rotate_ms":20000}
    /marquee/kitchen/cmd  =  {"brightness":40,"save":true}

Frames stay under about 1 KB, so send pixel data over the local network
instead.

## Why not mDNS everywhere?

On your own network you do not need any of this: the panel registers
`marquee.local` over mDNS and also asks your router for the hostname, so
`http://marquee.local/` and often plain `http://marquee/` both work. A relay is
only for reaching it from somewhere else.
