"""Minimal CLASP v3 client over WebSocket. Standard library only.

Implements just enough of the protocol to talk to a relay: HELLO, SUBSCRIBE,
SET and the frame codec. See https://github.com/lumencanvas/clasp
"""

import base64
import os
import socket
import ssl
import struct
import time
from urllib.parse import urlparse

MAGIC = 0x53
MSG_HELLO, MSG_WELCOME, MSG_SUBSCRIBE = 0x01, 0x02, 0x10
MSG_PUBLISH, MSG_SET, MSG_PING, MSG_PONG = 0x20, 0x21, 0x41, 0x42
VAL_BOOL, VAL_I32, VAL_F32, VAL_STRING = 0x01, 0x04, 0x06, 0x08
FLAGS_BINARY, FLAGS_HELLO = 0x01, 0x41


def _s16(b):
    return struct.pack("!H", len(b)) + b


class Clasp:
    """Connects to a CLASP relay and exchanges string parameters."""

    def __init__(self, url, name="marquee-tool", timeout=15):
        u = urlparse(url)
        tls = u.scheme == "wss"
        host = u.hostname
        port = u.port or (443 if tls else 80)
        path = u.path or "/"

        raw = socket.create_connection((host, port), timeout)
        self.sock = (
            ssl.create_default_context().wrap_socket(raw, server_hostname=host)
            if tls
            else raw
        )
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            (
                f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
                f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                f"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: clasp\r\n\r\n"
            ).encode()
        )
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise IOError("relay closed during handshake")
            buf += chunk
        if not buf.startswith(b"HTTP/1.1 101"):
            raise IOError("relay refused the upgrade: " + buf.split(b"\r\n")[0].decode())
        self.buf = buf.split(b"\r\n\r\n", 1)[1]

        self._frame(bytes([MSG_HELLO, 0x01, 0xE0]) + _s16(name.encode()) + _s16(b""),
                    FLAGS_HELLO)

    # --- websocket ---------------------------------------------------------
    def _send_ws(self, data, opcode=2):
        mask = os.urandom(4)
        n = len(data)
        head = bytearray([0x80 | opcode])
        if n < 126:
            head.append(0x80 | n)
        elif n < 65536:
            head.append(0x80 | 126)
            head += struct.pack("!H", n)
        else:
            head.append(0x80 | 127)
            head += struct.pack("!Q", n)
        head += mask
        head += bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(bytes(head))

    def _need(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(8192)
            if not chunk:
                raise IOError("relay closed")
            self.buf += chunk

    def _recv_ws(self):
        self._need(2)
        op = self.buf[0] & 0x0F
        n = self.buf[1] & 0x7F
        off = 2
        if n == 126:
            self._need(4)
            n = struct.unpack("!H", self.buf[2:4])[0]
            off = 4
        elif n == 127:
            self._need(10)
            n = struct.unpack("!Q", self.buf[2:10])[0]
            off = 10
        self._need(off + n)
        payload = bytes(self.buf[off : off + n])
        self.buf = self.buf[off + n :]
        if op == 9:
            self._send_ws(payload, opcode=10)
            return self._recv_ws()
        if op == 8:
            raise IOError("relay closed the connection")
        return payload

    # --- clasp -------------------------------------------------------------
    def _frame(self, payload, flags=FLAGS_BINARY):
        self._send_ws(bytes([MAGIC, flags]) + struct.pack("!H", len(payload)) + payload)

    def set(self, address, value):
        """Sets a string parameter."""
        self._frame(
            bytes([MSG_SET, VAL_STRING])
            + _s16(address.encode())
            + _s16(str(value).encode())
        )

    def subscribe(self, pattern, sub_id=1):
        self._frame(
            bytes([MSG_SUBSCRIBE])
            + struct.pack("!I", sub_id)
            + _s16(pattern.encode())
            + bytes([0xFF, 0x00])
        )

    def messages(self):
        """Yields (address, value) for every incoming SET."""
        while True:
            data = self._recv_ws()
            if len(data) < 5 or data[0] != MAGIC:
                continue
            p = data[4 : 4 + struct.unpack("!H", data[2:4])[0]]
            if not p:
                continue
            if p[0] == MSG_PING:
                self._frame(bytes([MSG_PONG]))
                continue
            if p[0] != MSG_SET or len(p) < 4:
                continue
            vtype = p[1] & 0x0F
            alen = struct.unpack("!H", p[2:4])[0]
            addr = p[4 : 4 + alen].decode("utf-8", "replace")
            v = p[4 + alen :]
            if vtype == VAL_STRING and len(v) >= 2:
                slen = struct.unpack("!H", v[0:2])[0]
                yield addr, v[2 : 2 + slen].decode("utf-8", "replace")
            elif vtype == VAL_I32 and len(v) >= 4:
                yield addr, struct.unpack("!i", v[0:4])[0]
            elif vtype == VAL_F32 and len(v) >= 4:
                yield addr, struct.unpack("!f", v[0:4])[0]
            elif vtype == VAL_BOOL and len(v) >= 1:
                yield addr, bool(v[0])

    def drain(self, seconds=0.3):
        """Reads and throws away whatever the relay sends on connect."""
        self.sock.settimeout(seconds)
        try:
            while True:
                self._recv_ws()
        except (socket.timeout, ssl.SSLWantReadError, OSError):
            pass
        finally:
            self.sock.settimeout(None)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
