#!/usr/bin/env python3
"""Forwards CLASP relay messages to a panel on the local network.

Run this on any always-on machine that can see the panel. It keeps the TLS
connection to the relay off the ESP8266, which has little memory to spare.

  ./clasp_bridge.py --device kitchen --host marquee.local

Then, from anywhere:

  ../tools/marquee.py --relay wss://relay.clasp.to --device kitchen say hi
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

from clasp import Clasp


def forward(host, cmd):
    req = urllib.request.Request(
        f"http://{host}/api/cmd",
        data=json.dumps(cmd).encode(),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            r.read()
        return True
    except (urllib.error.HTTPError, OSError) as e:
        print("  panel error:", e, file=sys.stderr)
        return False


def to_command(leaf, value):
    if leaf == "cmd":
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return None
    if leaf in ("say", "text"):
        return {"text": str(value)}
    if leaf == "anim":
        return {"anim": str(value), "now": True}
    if leaf == "brightness":
        return {"brightness": int(value)}
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--relay", default="wss://relay.clasp.to")
    p.add_argument("--device", required=True, help="name this panel answers to")
    p.add_argument("--host", default="marquee.local", help="panel on the LAN")
    args = p.parse_args()

    base = f"/marquee/{args.device}"
    print(f"bridging {args.relay}{base}/** -> http://{args.host}/api/cmd")

    while True:
        try:
            c = Clasp(args.relay, name=f"bridge-{args.device}")
            c.subscribe(f"{base}/**")
            c.drain()          # skip the relay's opening state snapshot
            c.set(f"{base}/status", "bridge online")
            print("connected")
            for address, value in c.messages():
                if not address.startswith(base + "/"):
                    continue
                leaf = address.rsplit("/", 1)[-1]
                if leaf == "status":
                    continue
                cmd = to_command(leaf, value)
                if cmd:
                    print(f"{address} = {value!r}")
                    forward(args.host, cmd)
        except KeyboardInterrupt:
            return
        except Exception as e:
            print("relay:", e, "- retrying in 5s", file=sys.stderr)
            time.sleep(5)


if __name__ == "__main__":
    main()
