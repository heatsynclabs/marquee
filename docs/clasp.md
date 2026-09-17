# Driving the panel from outside your network

`marquee.local` only works on your own network. To reach the panel from
anywhere, put a [CLASP](https://clasp.to) relay between you and it. CLASP is a
small binary pub/sub protocol; a relay is a server that clients connect to and
exchange addressed values through.

There are two ways to do it. The bridge is the one to start with.

## Option 1: a bridge on your network (recommended)

A short script holds the connection to the relay and forwards anything it
receives to the panel over HTTP. Run it on anything that is always on and can
see the panel, such as a Raspberry Pi or a home server.

    tools/clasp_bridge.py --device kitchen --host marquee.local

Then, from anywhere:

    tools/marquee.py --relay wss://relay.clasp.to --device kitchen say "on my way"

The ESP8266 never talks to the relay, so there is no TLS and no memory
pressure on it, and it keeps working normally if the bridge or the relay goes
away.

## Option 2: the panel connects to the relay itself

The firmware has its own CLASP client. Turn it on:

    curl -d '{"clasp_enabled":true,"clasp_url":"wss://relay.clasp.to","reboot":true}' \
         http://marquee.local/api/config

Find the device id it chose (it defaults to `esp` plus the chip id):

    curl -s http://marquee.local/api/state | grep device

The panel subscribes to `/marquee/<device>/**` and answers these addresses:

| Address | Value | Effect |
|---------|-------|--------|
| `/marquee/<device>/cmd` | JSON string | Any command from [api.md](api.md) |
| `/marquee/<device>/say` | string | Scroll a message |
| `/marquee/<device>/anim` | string | Run an effect |
| `/marquee/<device>/brightness` | int | Set brightness |

It publishes `/marquee/<device>/status` when it connects and every 30 seconds
after that.

### What to expect

Running this on an ESP8266 has real costs, and they are worth knowing before
you rely on it.

- **TLS is expensive.** A `wss://` connection needs roughly 20 KB of heap for
  its receive buffer, out of about 35 KB free. It works, but there is little
  headroom, and the handshake stalls the animation for a second or two on each
  reconnect. A `ws://` relay on your own network costs almost nothing.
- **The public relay greets every client with its full state**, which was
  around 53 KB when this was written and grows over time. That is far more
  than the board can hold, so the firmware reads past oversized frames and
  discards them rather than buffering. This is also why the firmware carries
  its own small WebSocket client instead of a library: the common Arduino
  WebSocket library drops the connection on any message over 15 KB, which
  would leave the panel in a reconnect loop.
- **The public relay is a shared, unauthenticated namespace.** Anyone who
  knows your device id can write to it, and anyone watching can see what you
  send. Pick an unguessable device id, or run your own relay.
- Certificates are not verified. The traffic is encrypted, but a relay that
  is not the one you meant would not be detected.

### Running your own relay

    git clone https://github.com/lumencanvas/clasp
    cd clasp/deploy/relay
    cargo run --release

It listens on port 7330. Point the panel at it with
`ws://<that-host>:7330` and none of the costs above apply.

## Why not mDNS everywhere?

On your own network you do not need any of this: the panel registers
`marquee.local` over mDNS and also asks your router for the hostname, so
`http://marquee.local/` and often plain `http://marquee/` both work. The relay
is only for reaching it from somewhere else.
