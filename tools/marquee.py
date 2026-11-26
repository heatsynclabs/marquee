#!/usr/bin/env python3
"""Command line client for a Marquee panel.

  marquee.py say "hello world" --color red
  marquee.py anim fire
  marquee.py bright 90
  marquee.py upload logo demo.gif --fps 12
  marquee.py stream demo.gif --loop

Talks to the panel over HTTP by default, or through a CLASP relay with
--relay. Pillow is only needed for the image and GIF commands.
"""

import argparse
import base64
import json
import socket
import sys
import time
import urllib.error
import urllib.request

W, H = 30, 8
UDP_PORT = 21324


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


# --- transports --------------------------------------------------------------

def post(host, path, body, method="POST", content_type="application/json"):
    url = f"http://{host}{path}"
    data = body if isinstance(body, bytes) else body.encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": content_type})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        die(f"{e.code}: {e.read().decode(errors='replace')}")
    except OSError as e:
        die(f"cannot reach {host}: {e}")


def get(host, path):
    try:
        with urllib.request.urlopen(f"http://{host}{path}", timeout=10) as r:
            return json.loads(r.read())
    except OSError as e:
        die(f"cannot reach {host}: {e}")


def send(args, cmd):
    """Delivers one command object, locally or through the relay."""
    if args.relay:
        from clasp import Clasp
        if not args.device:
            die("--relay needs --device (the panel's clasp device id)")
        c = Clasp(args.relay)
        c.drain()
        c.set(f"/marquee/{args.device}/cmd", json.dumps(cmd))
        time.sleep(0.3)
        c.close()
        return {"ok": True}
    return post(args.host, "/api/cmd", json.dumps(cmd))


def udp_frame(host, pixels, hold=2):
    """Pushes one raw frame using the WLED-compatible realtime protocol."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(bytes([2, hold]) + pixels, (host, UDP_PORT))
    s.close()


# --- images ------------------------------------------------------------------

def load_frames(path, fit="cover"):
    try:
        from PIL import Image, ImageSequence
    except ImportError:
        die("this command needs Pillow:  pip install pillow")
    img = Image.open(path)
    out = []
    for frame in ImageSequence.Iterator(img):
        f = frame.convert("RGB")
        if fit == "stretch":
            f = f.resize((W, H))
        else:
            scale = max(W / f.width, H / f.height)
            f = f.resize((max(1, round(f.width * scale)), max(1, round(f.height * scale))))
            left = (f.width - W) // 2
            top = (f.height - H) // 2
            f = f.crop((left, top, left + W, top + H))
        out.append(f.tobytes())
    return out


# --- commands ----------------------------------------------------------------

def cmd_say(args):
    cmd = {"text": " ".join(args.text), "color": args.color, "speed": args.speed,
           "repeat": args.repeat, "scroll": not args.static}
    if args.now:
        cmd["now"] = True
    print(send(args, cmd))


def cmd_anim(args):
    cmd = {"anim": args.name, "now": True}
    if args.hold:
        cmd["hold"] = args.hold
    print(send(args, cmd))


def cmd_idle(args):
    print(send(args, {"idle": args.name, "save": True}))


def cmd_bright(args):
    print(send(args, {"brightness": args.value, "save": True}))


def cmd_clear(args):
    print(send(args, {"clear": True}))


def cmd_play(args):
    print(send(args, {"play": args.name, "now": True, "repeat": args.repeat}))


def cmd_state(args):
    print(json.dumps(get(args.host, "/api/state"), indent=2))


def cmd_image(args):
    frames = load_frames(args.file, args.fit)
    udp_frame(args.host, frames[0], args.hold)
    print(f"sent 1 frame, holding {args.hold}s")


def cmd_stream(args):
    frames = load_frames(args.file, args.fit)
    delay = 1.0 / args.fps
    print(f"streaming {len(frames)} frames at {args.fps} fps; ctrl-c to stop")
    try:
        while True:
            for f in frames:
                udp_frame(args.host, f, 2)
                time.sleep(delay)
            if not args.loop:
                break
    except KeyboardInterrupt:
        pass


def cmd_upload(args):
    frames = load_frames(args.file, args.fit)
    post(args.host, f"/api/anim?name={args.name}&fps={args.fps}", b"")
    for i, f in enumerate(frames, 1):
        post(args.host, f"/api/anim/frame?name={args.name}",
             base64.b64encode(f), content_type="text/plain")
        print(f"\r{i}/{len(frames)}", end="", flush=True)
    print(f"\nstored as '{args.name}' - play it with: marquee.py play {args.name}")


def cmd_rotate(args):
    """Shows or edits the idle rotation."""
    rot = get(args.host, "/api/rotate")
    changed = False
    if args.ms:
        rot["ms"] = args.ms
        changed = True
    if args.set:
        rot["items"] = [json.loads(x) if x.lstrip().startswith("{") else {"anim": x}
                        for x in args.set]
        changed = True
    if args.open is not None:
        rot["open"] = {"cards": args.open, "color": args.open_color} if args.open else False
        changed = True
    if changed:
        post(args.host, "/api/rotate", json.dumps(rot))
        rot = get(args.host, "/api/rotate")
    print(json.dumps(rot, indent=2))


def cmd_status(args):
    st = get(args.host, "/api/state")["status"]
    if args.poll:
        post(args.host, "/api/status/poll", b"")
        time.sleep(3)
        st = get(args.host, "/api/state")["status"]
    print(json.dumps(st, indent=2))


def cmd_rm(args):
    print(post(args.host, f"/api/anim?name={args.name}", b"", method="DELETE"))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="marquee.local", help="panel hostname or IP")
    p.add_argument("--relay", help="CLASP relay URL, e.g. wss://relay.clasp.to")
    p.add_argument("--device", help="clasp device id when using --relay")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("say", help="scroll a message")
    s.add_argument("text", nargs="+")
    s.add_argument("--color", default="white", help="name, #rrggbb, or 'rainbow'")
    s.add_argument("--speed", type=int, default=20, help="pixels per second")
    s.add_argument("--repeat", type=int, default=1)
    s.add_argument("--static", action="store_true", help="do not scroll")
    s.add_argument("--now", action="store_true", help="interrupt what is playing")
    s.set_defaults(func=cmd_say)

    s = sub.add_parser("anim", help="run a built-in animation")
    s.add_argument("name")
    s.add_argument("--hold", type=int, help="milliseconds, then move on")
    s.set_defaults(func=cmd_anim)

    s = sub.add_parser("idle", help="set what shows when the queue is empty")
    s.add_argument("name")
    s.set_defaults(func=cmd_idle)

    s = sub.add_parser("bright", help="set brightness 0-255")
    s.add_argument("value", type=int)
    s.set_defaults(func=cmd_bright)

    sub.add_parser("clear", help="empty the queue").set_defaults(func=cmd_clear)
    sub.add_parser("state", help="print panel status").set_defaults(func=cmd_state)

    s = sub.add_parser("play", help="play a stored animation")
    s.add_argument("name")
    s.add_argument("--repeat", type=int, default=3)
    s.set_defaults(func=cmd_play)

    s = sub.add_parser("image", help="show one image (needs Pillow)")
    s.add_argument("file")
    s.add_argument("--hold", type=int, default=10, help="seconds to hold")
    s.add_argument("--fit", choices=["cover", "stretch"], default="cover")
    s.set_defaults(func=cmd_image)

    s = sub.add_parser("stream", help="stream a GIF live (needs Pillow)")
    s.add_argument("file")
    s.add_argument("--fps", type=int, default=15)
    s.add_argument("--loop", action="store_true")
    s.add_argument("--fit", choices=["cover", "stretch"], default="cover")
    s.set_defaults(func=cmd_stream)

    s = sub.add_parser("upload", help="store a GIF on the panel (needs Pillow)")
    s.add_argument("name")
    s.add_argument("file")
    s.add_argument("--fps", type=int, default=12)
    s.add_argument("--fit", choices=["cover", "stretch"], default="cover")
    s.set_defaults(func=cmd_upload)

    s = sub.add_parser("rotate", help="show or change the idle rotation")
    s.add_argument("--set", nargs="+", metavar="ENTRY",
                   help="effect names, or JSON command objects")
    s.add_argument("--ms", type=int, help="how long each entry stays up")
    s.add_argument("--open", metavar="FRAMES",
                   help="frames to show while the status poll is true, split with |, "
                        "e.g. \"OPEN|OPEN HOURS {bot} COME IN\"; empty string turns it off")
    s.add_argument("--open-color", default="#33ff66")
    s.set_defaults(func=cmd_rotate)

    s = sub.add_parser("status", help="show the status poll")
    s.add_argument("--poll", action="store_true", help="poll now and wait for the result")
    s.set_defaults(func=cmd_status)

    s = sub.add_parser("rm", help="delete a stored animation")
    s.add_argument("name")
    s.set_defaults(func=cmd_rm)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
