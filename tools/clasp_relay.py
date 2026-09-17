#!/usr/bin/env python3
"""A tiny CLASP relay for your own network. Standard library only.

The public relay at wss://relay.clasp.to needs TLS, which is expensive on an
ESP8266. Running this instead gives the panel a plain ws:// relay it can hold
open cheaply, and keeps your messages on your own network.

  ./clasp_relay.py                      listens on 0.0.0.0:7330
  # then point the panel at ws://<this machine>:7330

It speaks enough of CLASP v3 for the panel and for tools/marquee.py: HELLO,
SUBSCRIBE with * and ** wildcards, SET, and PING. State is kept in memory and
replayed to late subscribers.
"""

import argparse
import base64
import hashlib
import os
import re
import socket
import struct
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAGIC = 0x53
MSG_HELLO, MSG_WELCOME, MSG_SUBSCRIBE, MSG_UNSUBSCRIBE = 0x01, 0x02, 0x10, 0x11
MSG_SET, MSG_PING, MSG_PONG = 0x21, 0x41, 0x42
VAL_STRING = 0x08

lock = threading.Lock()
clients = []          # list of Client
state = {}            # address -> (vtype, raw value bytes)


def s16(b):
    return struct.pack("!H", len(b)) + b


def frame(payload, flags=0x01):
    return bytes([MAGIC, flags]) + struct.pack("!H", len(payload)) + payload


def matches(pattern, address):
    """CLASP wildcards: * is one segment, ** is any number."""
    rx = "^" + re.escape(pattern).replace(r"\*\*", "§§").replace(r"\*", "[^/]+") \
                                 .replace("§§", ".*") + "$"
    return re.match(rx, address) is not None


class Client(threading.Thread):
    def __init__(self, sock, addr):
        super().__init__(daemon=True)
        self.sock = sock
        self.addr = addr
        self.name = "?"
        self.subs = []
        self.buf = b""
        self.alive = True

    # --- websocket ---
    def handshake(self):
        while b"\r\n\r\n" not in self.buf:
            d = self.sock.recv(4096)
            if not d:
                return False
            self.buf += d
        head, _, rest = self.buf.partition(b"\r\n\r\n")
        self.buf = rest
        m = re.search(rb"Sec-WebSocket-Key:\s*(\S+)", head, re.I)
        if not m:
            return False
        accept = base64.b64encode(
            hashlib.sha1(m.group(1) + GUID.encode()).digest()).decode()
        proto = b"Sec-WebSocket-Protocol: clasp\r\n" if b"clasp" in head else b""
        self.sock.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            b"Connection: Upgrade\r\n" + proto +
            f"Sec-WebSocket-Accept: {accept}\r\n\r\n".encode())
        return True

    def send_ws(self, data, opcode=2):
        n = len(data)
        head = bytearray([0x80 | opcode])
        if n < 126:
            head.append(n)
        elif n < 65536:
            head.append(126)
            head += struct.pack("!H", n)
        else:
            head.append(127)
            head += struct.pack("!Q", n)
        try:
            self.sock.sendall(bytes(head) + data)
        except OSError:
            self.alive = False

    def need(self, n):
        while len(self.buf) < n:
            d = self.sock.recv(4096)
            if not d:
                raise IOError("closed")
            self.buf += d

    def recv_ws(self):
        self.need(2)
        b0, b1 = self.buf[0], self.buf[1]
        op, masked, n = b0 & 0x0F, b1 & 0x80, b1 & 0x7F
        off = 2
        if n == 126:
            self.need(4)
            n = struct.unpack("!H", self.buf[2:4])[0]
            off = 4
        elif n == 127:
            self.need(10)
            n = struct.unpack("!Q", self.buf[2:10])[0]
            off = 10
        key = b""
        if masked:
            self.need(off + 4)
            key = self.buf[off:off + 4]
            off += 4
        self.need(off + n)
        data = bytearray(self.buf[off:off + n])
        self.buf = self.buf[off + n:]
        if masked:
            for i in range(n):
                data[i] ^= key[i % 4]
        if op == 8:
            raise IOError("close")
        if op == 9:
            self.send_ws(bytes(data), opcode=10)
            return self.recv_ws()
        return bytes(data)

    # --- clasp ---
    def send_clasp(self, payload, flags=0x01):
        self.send_ws(frame(payload, flags))

    def run(self):
        try:
            if not self.handshake():
                return
            with lock:
                clients.append(self)
            while self.alive:
                self.on_frame(self.recv_ws())
        except (OSError, IOError):
            pass
        finally:
            with lock:
                if self in clients:
                    clients.remove(self)
            self.sock.close()
            print(f"- {self.name} left")

    def on_frame(self, data):
        if len(data) < 5 or data[0] != MAGIC:
            return
        n = struct.unpack("!H", data[2:4])[0]
        p = data[4:4 + n]
        if not p:
            return
        t = p[0]
        if t == MSG_HELLO:
            alen = struct.unpack("!H", p[3:5])[0]
            self.name = p[5:5 + alen].decode("utf-8", "replace") or "?"
            print(f"+ {self.name} joined from {self.addr[0]}")
            # WELCOME: version, features, u16+session id, u16+server name
            self.send_clasp(bytes([MSG_WELCOME, 0x01, 0xE0]) +
                            s16(b"local") + s16(b"clasp_relay.py"))
        elif t == MSG_PING:
            self.send_clasp(bytes([MSG_PONG]))
        elif t == MSG_SUBSCRIBE:
            plen = struct.unpack("!H", p[5:7])[0]
            pattern = p[7:7 + plen].decode("utf-8", "replace")
            self.subs.append(pattern)
            print(f"  {self.name} subscribed to {pattern}")
            with lock:
                for addr, (vtype, raw) in state.items():
                    if matches(pattern, addr):
                        self.send_clasp(bytes([MSG_SET, vtype]) + s16(addr.encode()) + raw)
        elif t == MSG_SET:
            vtype = p[1] & 0x0F
            alen = struct.unpack("!H", p[2:4])[0]
            addr = p[4:4 + alen].decode("utf-8", "replace")
            raw = p[4 + alen:]
            with lock:
                state[addr] = (vtype, raw)
                targets = [c for c in clients
                           if c is not self and any(matches(s, addr) for s in c.subs)]
            body = bytes([MSG_SET, vtype]) + s16(addr.encode()) + raw
            shown = raw[2:].decode("utf-8", "replace") if vtype == VAL_STRING else raw.hex()
            print(f"  {addr} = {shown[:60]}  -> {len(targets)} listener(s)")
            for c in targets:
                c.send_clasp(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=7330)
    args = ap.parse_args()

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(8)
    print(f"clasp relay on ws://{args.host}:{args.port}")
    while True:
        sock, addr = srv.accept()
        Client(sock, addr).start()


if __name__ == "__main__":
    main()
