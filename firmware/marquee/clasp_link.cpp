#include "clasp_link.h"
#include "commands.h"
#include "settings.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

namespace {

// CLASP v3 binary protocol, as documented in the clasp repository.
const uint8_t MAGIC        = 0x53;  // 'S'
const uint8_t MSG_HELLO    = 0x01;
const uint8_t MSG_WELCOME  = 0x02;
const uint8_t MSG_SUBSCRIBE= 0x10;
const uint8_t MSG_PUBLISH  = 0x20;
const uint8_t MSG_SET      = 0x21;
const uint8_t MSG_PING     = 0x41;
const uint8_t MSG_PONG     = 0x42;
const uint8_t VAL_BOOL     = 0x01;
const uint8_t VAL_I32      = 0x04;
const uint8_t VAL_F32      = 0x06;
const uint8_t VAL_STRING   = 0x08;
const uint8_t FLAGS_BINARY = 0x01;
const uint8_t FLAGS_HELLO  = 0x41;  // binary + QoS confirm

const size_t   kRxMax       = 1024;  // larger frames are skipped, not buffered
const size_t   kTxMax       = 256;
const uint32_t kRetryMs     = 15000;
const uint32_t kPingMs      = 30000;
const uint32_t kTlsHeapNeed = 26000;

WiFiClient* sock = nullptr;
bool  secure = false;
char  host[64] = "";
uint16_t port = 80;
char  path[32] = "/";
char  device[32] = "";
char  base[48] = "";  // "/marquee/<device>"

enum State : uint8_t { Down, Handshaking, Open } state = Down;
uint32_t nextTry = 0, lastRx = 0, lastPing = 0;

uint8_t  rx[kRxMax];
size_t   rxLen = 0;
uint32_t skipLeft = 0;   // bytes of an oversized frame still to discard
uint8_t  hdr[14];
size_t   hdrLen = 0, hdrNeed = 2;
uint64_t payloadLeft = 0;
bool     framePayload = false;
uint8_t  frameOp = 0;
bool     frameMasked = false;

void reset(bool retry = true) {
  if (sock) {
    sock->stop();
    delete sock;
    sock = nullptr;
  }
  state = Down;
  rxLen = hdrLen = 0;
  hdrNeed = 2;
  skipLeft = 0;
  framePayload = false;
  if (retry) nextTry = millis() + kRetryMs;
}

bool parseUrl(const char* url) {
  const char* p = url;
  if (!strncmp(p, "wss://", 6)) { secure = true; p += 6; }
  else if (!strncmp(p, "ws://", 5)) { secure = false; p += 5; }
  else return false;

  const char* slash = strchr(p, '/');
  const char* colon = strchr(p, ':');
  if (colon && slash && colon > slash) colon = nullptr;

  size_t hostLen = colon ? (size_t)(colon - p) : (slash ? (size_t)(slash - p) : strlen(p));
  if (hostLen == 0 || hostLen >= sizeof host) return false;
  memcpy(host, p, hostLen);
  host[hostLen] = 0;

  port = secure ? 443 : 80;
  if (colon) port = atoi(colon + 1);
  strlcpy(path, slash ? slash : "/", sizeof path);
  return true;
}

// --- WebSocket ---------------------------------------------------------------

void sendWs(uint8_t opcode, const uint8_t* data, size_t len) {
  if (!sock || !sock->connected() || len > kTxMax) return;
  uint8_t out[kTxMax + 8];
  size_t n = 0;
  out[n++] = 0x80 | opcode;
  if (len < 126) out[n++] = 0x80 | len;
  else { out[n++] = 0x80 | 126; out[n++] = len >> 8; out[n++] = len & 0xFF; }
  uint8_t mask[4];
  for (int i = 0; i < 4; i++) mask[i] = RANDOM_REG32 & 0xFF;
  memcpy(out + n, mask, 4);
  n += 4;
  for (size_t i = 0; i < len; i++) out[n + i] = data[i] ^ mask[i & 3];
  sock->write(out, n + len);
}

bool handshake() {
  char key[25] = "QUFCQkNDRERFRUZGR0dISA==";  // any 16-byte value works
  sock->printf("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: clasp\r\n\r\n",
               path, host, key);
  return true;
}

// --- CLASP -------------------------------------------------------------------

size_t putStr(uint8_t* b, const char* s) {
  uint16_t n = strlen(s);
  b[0] = n >> 8;
  b[1] = n & 0xFF;
  memcpy(b + 2, s, n);
  return 2 + n;
}

void sendClasp(const uint8_t* payload, uint16_t len, uint8_t flags = FLAGS_BINARY) {
  uint8_t frame[kTxMax];
  if ((size_t)len + 4 > sizeof frame) return;
  frame[0] = MAGIC;
  frame[1] = flags;
  frame[2] = len >> 8;
  frame[3] = len & 0xFF;
  memcpy(frame + 4, payload, len);
  sendWs(0x2, frame, len + 4);
}

void sendHello() {
  uint8_t p[96];
  size_t n = 0;
  p[n++] = MSG_HELLO;
  p[n++] = 0x01;  // protocol version
  p[n++] = 0xE0;  // params + events + streams
  n += putStr(p + n, device);
  p[n++] = 0;     // no auth token
  p[n++] = 0;
  sendClasp(p, n, FLAGS_HELLO);
}

void sendSubscribe() {
  char pattern[64];
  snprintf(pattern, sizeof pattern, "%s/**", base);
  uint8_t p[96];
  size_t n = 0;
  p[n++] = MSG_SUBSCRIBE;
  p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 1;  // subscription id
  n += putStr(p + n, pattern);
  p[n++] = 0xFF;  // all signal types
  p[n++] = 0x00;  // no options
  sendClasp(p, n);
}

void publishStatus(const char* value) {
  char addr[64];
  snprintf(addr, sizeof addr, "%s/status", base);
  uint8_t p[160];
  size_t n = 0;
  p[n++] = MSG_SET;
  p[n++] = VAL_STRING;
  n += putStr(p + n, addr);
  n += putStr(p + n, value);
  sendClasp(p, n);
}

void sendPong() {
  uint8_t p[1] = {MSG_PONG};
  sendClasp(p, 1);
}

String take(const char* p, size_t n) {
  String s;
  s.reserve(n + 1);
  for (size_t i = 0; i < n; i++) s += p[i];
  return s;
}

// Routes one incoming address/value pair into the command interpreter.
void dispatch(const char* addr, const char* value, size_t valueLen, int32_t num, bool isNum) {
  const char* leaf = strrchr(addr, '/');
  leaf = leaf ? leaf + 1 : addr;

  String err;
  if (!strcmp(leaf, "cmd")) {
    Commands::applyJson(take(value, valueLen), err);
  } else if (!strcmp(leaf, "say") || !strcmp(leaf, "text")) {
    JsonDocument doc;
    doc["text"] = take(value, valueLen);
    Commands::apply(doc.as<JsonObjectConst>(), err);
  } else if (!strcmp(leaf, "anim")) {
    JsonDocument doc;
    doc["anim"] = take(value, valueLen);
    doc["now"] = true;
    Commands::apply(doc.as<JsonObjectConst>(), err);
  } else if (!strcmp(leaf, "brightness") && isNum) {
    JsonDocument doc;
    doc["brightness"] = num;
    Commands::apply(doc.as<JsonObjectConst>(), err);
  }
}

// payload is one decoded CLASP frame body (message type first).
void onClasp(const uint8_t* p, size_t len) {
  if (!len) return;
  switch (p[0]) {
    case MSG_WELCOME:
      sendSubscribe();
      publishStatus("online");
      Serial.println("clasp: connected");
      break;
    case MSG_PING:
      sendPong();
      break;
    case MSG_SET: {
      if (len < 4) return;
      uint8_t vtype = p[1] & 0x0F;
      uint16_t alen = (p[2] << 8) | p[3];
      if (4u + alen > len) return;
      char addr[80];
      size_t an = alen < sizeof addr - 1 ? alen : sizeof addr - 1;
      memcpy(addr, p + 4, an);
      addr[an] = 0;

      const uint8_t* v = p + 4 + alen;
      size_t vlen = len - 4 - alen;
      if (vtype == VAL_STRING && vlen >= 2) {
        uint16_t slen = (v[0] << 8) | v[1];
        if (2u + slen <= vlen) dispatch(addr, (const char*)(v + 2), slen, 0, false);
      } else if (vtype == VAL_I32 && vlen >= 4) {
        int32_t n = ((int32_t)v[0] << 24) | ((int32_t)v[1] << 16) | (v[2] << 8) | v[3];
        dispatch(addr, "", 0, n, true);
      } else if (vtype == VAL_F32 && vlen >= 4) {
        uint32_t bits = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | (v[2] << 8) | v[3];
        float f;
        memcpy(&f, &bits, 4);
        dispatch(addr, "", 0, (int32_t)f, true);
      } else if (vtype == VAL_BOOL && vlen >= 1) {
        dispatch(addr, "", 0, v[0] ? 1 : 0, true);
      }
      break;
    }
    default:
      break;  // snapshots, acks and everything else are not needed here
  }
}

void onWsPayload(const uint8_t* p, size_t len) {
  if (len < 4 || p[0] != MAGIC) return;
  uint16_t plen = (p[2] << 8) | p[3];
  if (4u + plen > len) return;
  onClasp(p + 4, plen);
}

// --- Receive state machine ---------------------------------------------------

void pump() {
  while (sock && sock->available()) {
    lastRx = millis();

    if (skipLeft) {
      uint8_t junk[64];
      size_t want = skipLeft < sizeof junk ? skipLeft : sizeof junk;
      int n = sock->read(junk, want);
      if (n <= 0) return;
      skipLeft -= n;
      continue;
    }

    if (framePayload) {
      size_t want = payloadLeft < (kRxMax - rxLen) ? (size_t)payloadLeft : (kRxMax - rxLen);
      int n = sock->read(rx + rxLen, want);
      if (n <= 0) return;
      rxLen += n;
      payloadLeft -= n;
      if (payloadLeft == 0) {
        framePayload = false;
        if (frameOp == 0x2 || frameOp == 0x1) onWsPayload(rx, rxLen);
        else if (frameOp == 0x9) sendWs(0xA, rx, rxLen);   // ping -> pong
        else if (frameOp == 0x8) { reset(); return; }      // close
        rxLen = 0;
        hdrLen = 0;
        hdrNeed = 2;
      }
      continue;
    }

    int c = sock->read();
    if (c < 0) return;
    hdr[hdrLen++] = (uint8_t)c;

    if (hdrLen == 2) {
      frameOp = hdr[0] & 0x0F;
      frameMasked = hdr[1] & 0x80;
      uint8_t l = hdr[1] & 0x7F;
      hdrNeed = 2 + (l == 126 ? 2 : (l == 127 ? 8 : 0)) + (frameMasked ? 4 : 0);
    }
    if (hdrLen < hdrNeed) continue;

    uint8_t l = hdr[1] & 0x7F;
    uint64_t plen = l;
    if (l == 126) plen = ((uint64_t)hdr[2] << 8) | hdr[3];
    else if (l == 127) {
      plen = 0;
      for (int i = 0; i < 8; i++) plen = (plen << 8) | hdr[2 + i];
    }

    hdrLen = 0;
    hdrNeed = 2;
    rxLen = 0;

    if (plen == 0) continue;
    if (plen > kRxMax) {
      // Almost always the relay's opening state snapshot. Read past it.
      skipLeft = plen;
    } else {
      framePayload = true;
      payloadLeft = plen;
    }
  }
}

void readHandshake() {
  // Wait for the end of the HTTP response headers, then start reading frames.
  while (sock && sock->available()) {
    int c = sock->read();
    if (c < 0) return;
    rx[rxLen++] = (uint8_t)c;
    if (rxLen >= 4 && !memcmp(rx + rxLen - 4, "\r\n\r\n", 4)) {
      bool ok = rxLen >= 12 && !memcmp(rx, "HTTP/1.1 101", 12);
      rxLen = 0;
      if (!ok) {
        Serial.println("clasp: handshake refused");
        reset();
        return;
      }
      state = Open;
      lastPing = millis();
      sendHello();
      return;
    }
    if (rxLen >= kRxMax) { reset(); return; }
  }
}

void connect() {
  if (!parseUrl(settings.claspUrl)) {
    Serial.println("clasp: bad url");
    nextTry = millis() + 60000;
    return;
  }
  if (secure && ESP.getFreeHeap() < kTlsHeapNeed) {
    Serial.println("clasp: not enough heap for TLS, retrying");
    nextTry = millis() + kRetryMs;
    return;
  }

  if (secure) {
    BearSSL::WiFiClientSecure* tls = new BearSSL::WiFiClientSecure();
    tls->setInsecure();
    tls->setBufferSizes(16384, 512);
    sock = tls;
  } else {
    sock = new WiFiClient();
  }
  sock->setTimeout(8000);

  Serial.printf("clasp: connecting to %s:%u\n", host, port);
  if (!sock->connect(host, port)) {
    Serial.println("clasp: connect failed");
    reset();
    return;
  }
  handshake();
  state = Handshaking;
  rxLen = 0;
  lastRx = millis();
}

}  // namespace

void ClaspLink::begin() {
  if (!settings.claspDevice[0]) {
    snprintf(settings.claspDevice, sizeof settings.claspDevice, "esp%06x", ESP.getChipId());
    settings.save();
  }
  strlcpy(device, settings.claspDevice, sizeof device);
  snprintf(base, sizeof base, "/marquee/%s", device);
}

void ClaspLink::loop() {
  if (!settings.claspEnabled || WiFi.status() != WL_CONNECTED) {
    if (state != Down) reset();
    return;
  }

  if (state == Down) {
    if ((int32_t)(millis() - nextTry) >= 0) connect();
    return;
  }

  if (!sock->connected() && !sock->available()) {
    Serial.println("clasp: disconnected");
    reset();
    return;
  }

  if (state == Handshaking) {
    readHandshake();
    if (millis() - lastRx > 10000) reset();
    return;
  }

  pump();

  if (millis() - lastPing > kPingMs) {
    lastPing = millis();
    publishStatus("online");
  }
}

bool ClaspLink::connected() { return state == Open; }
const char* ClaspLink::deviceId() { return device; }
