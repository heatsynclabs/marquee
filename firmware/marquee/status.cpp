#include "status.h"
#include "config.h"
#include "settings.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "player.h"
#include <time.h>

namespace {

bool     value = false;
bool     ok = false;
uint32_t lastOk = 0;
uint32_t nextPoll = 0;
uint32_t pollMs = 0;
char     error[40] = "not polled yet";
char     until[6] = "";   // "HH:MM" from the feed, or empty

// Matches "<key>" in a stream and returns the first character of its value,
// so the body never has to be buffered. Skips string values, which is what
// tells the real "open":true apart from an "open":"...url..." icon field.
class KeyScanner {
 public:
  explicit KeyScanner(const char* key) : key_(key), len_(strlen(key)) {}

  // Returns 't', 'f' or 0 when nothing has been decided yet.
  char feed(char c) {
    switch (stage_) {
      case Key:
        if (c == '"') { pos_ = 0; stage_ = InKey; }
        break;
      case InKey:
        if (pos_ < len_ && c == key_[pos_]) {
          pos_++;
        } else if (pos_ == len_ && c == '"') {
          stage_ = AfterKey;
        } else {
          stage_ = Key;
          if (c == '"') { pos_ = 0; stage_ = InKey; }
        }
        break;
      case AfterKey:
        if (c == ':' || c == ' ') break;
        stage_ = Key;
        if (c == 't' || c == 'f') return c;
        break;
    }
    return 0;
  }

 private:
  enum Stage : uint8_t { Key, InKey, AfterKey } stage_ = Key;
  const char* key_;
  size_t len_;
  size_t pos_ = 0;
};

// Like KeyScanner, but captures a short string value: "<key>":"21:00".
class StringScanner {
 public:
  StringScanner(const char* key, char* out, size_t n)
      : key_(key), len_(strlen(key)), out_(out), cap_(n) { out_[0] = 0; }

  bool done() const { return stage_ == Done; }

  void feed(char c) {
    switch (stage_) {
      case Key:
        if (c == '"') { pos_ = 0; stage_ = InKey; }
        break;
      case InKey:
        if (pos_ < len_ && c == key_[pos_]) pos_++;
        else if (pos_ == len_ && c == '"') stage_ = AfterKey;
        else { stage_ = Key; if (c == '"') { pos_ = 0; stage_ = InKey; } }
        break;
      case AfterKey:
        if (c == ':' || c == ' ') break;
        if (c == '"') { stage_ = InValue; w_ = 0; break; }
        stage_ = Done;  // null or a number: no value
        break;
      case InValue:
        if (c == '"') { out_[w_] = 0; stage_ = Done; break; }
        if (w_ + 1 < cap_) out_[w_++] = c;
        break;
      case Done:
        break;
    }
  }

 private:
  enum Stage : uint8_t { Key, InKey, AfterKey, InValue, Done } stage_ = Key;
  const char* key_;
  size_t len_, pos_ = 0;
  char* out_;
  size_t cap_, w_ = 0;
};

void fail(const char* why) {
  strlcpy(error, why, sizeof error);
  Serial.printf("status: %s\n", why);
}



// Splits "https://host[:port]/path" into its parts.
bool splitUrl(const char* url, bool& tls, char* host, size_t hostN,
              uint16_t& port, char* path, size_t pathN) {
  const char* p = url;
  if (!strncmp(p, "https://", 8)) { tls = true; p += 8; }
  else if (!strncmp(p, "http://", 7)) { tls = false; p += 7; }
  else return false;

  const char* slash = strchr(p, '/');
  const char* colon = strchr(p, ':');
  if (colon && slash && colon > slash) colon = nullptr;
  size_t n = colon ? (size_t)(colon - p) : (slash ? (size_t)(slash - p) : strlen(p));
  if (!n || n >= hostN) return false;
  memcpy(host, p, n);
  host[n] = 0;
  port = colon ? atoi(colon + 1) : (tls ? 443 : 80);
  strlcpy(path, slash ? slash : "/", pathN);
  return true;
}

// The client is created once and reused. Allocating and freeing a TLS client
// every few minutes is what slowly fragments the heap on an ESP8266.
BearSSL::WiFiClientSecure* tlsClient = nullptr;
WiFiClient plainClient;

uint32_t failures = 0;
uint32_t pushedAt = 0;   // last time someone pushed the value to us

uint32_t pollIntervalMs() {
  return (uint32_t)(settings.statusPollS ? settings.statusPollS : 300) * 1000;
}

// Blocks for about half a second at 160 MHz (most of it the TLS handshake).
// Status::loop picks a moment when the panel is showing something still.
void poll() {
  uint32_t t0 = millis();
  bool tls;
  char host[80], path[96];
  uint16_t port;
  if (!splitUrl(settings.statusUrl, tls, host, sizeof host, port, path, sizeof path))
    return fail("bad status url");

  // Largest free block, not total free: a fragmented heap has plenty of
  // free bytes and still cannot fit a TLS buffer.
  const uint32_t need = tls ? STATUS_TLS_BUFFER + 12000 : 3000;
  if (ESP.getMaxFreeBlockSize() < need) return fail("low heap, skipped");

  // A short lookup first: the default one can block for many seconds when the
  // internet is down. It also leaves the answer in lwIP's cache.
  IPAddress ip;
  if (!WiFi.hostByName(host, ip, 3000)) return fail("dns failed");

  WiFiClient* client;
  if (tls) {
    if (!tlsClient) {
      tlsClient = new BearSSL::WiFiClientSecure();
      tlsClient->setInsecure();
      tlsClient->setBufferSizes(STATUS_TLS_BUFFER, 512);
    }
    client = tlsClient;
  } else {
    client = &plainClient;
  }
  client->setTimeout(4000);

  // TLS connects by name so the server gets SNI.
  bool connected = tls ? tlsClient->connect(host, port) : client->connect(ip, port);
  if (!connected) {
    client->stop();
    return fail("connect failed");
  }
  client->printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                 "User-Agent: marquee\r\n\r\n", path, host);

  // Reads to the end of the body (a SpaceAPI document is about 1.5 KB) so
  // the optional closing time is seen wherever it sits.
  KeyScanner scan(settings.statusKey);
  char gotUntil[6];
  StringScanner untilScan("open_until", gotUntil, sizeof gotUntil);
  char found = 0;
  uint32_t deadline = millis() + 5000;
  size_t bytes = 0;
  while ((client->connected() || client->available()) &&
         (int32_t)(millis() - deadline) < 0 && !(found && untilScan.done()) &&
         bytes < 16384) {
    while (client->available() && !(found && untilScan.done())) {
      char c = (char)client->read();
      bytes++;
      if (!found) found = scan.feed(c);
      untilScan.feed(c);
    }
    delay(1);
  }
  client->stop();
  strlcpy(until, untilScan.done() ? gotUntil : "", sizeof until);

  if (!found) return fail("key not found");

  pollMs = millis() - t0;
  value = (found == 't');
  ok = true;
  lastOk = millis();
  failures = 0;
  strlcpy(error, "ok", sizeof error);
  Serial.printf("status: %s = %s (%lu ms)\n", settings.statusKey,
                value ? "true" : "false", (unsigned long)pollMs);
}

}  // namespace

void Status::begin() { nextPoll = millis() + 5000; }

void Status::pollNow() { nextPoll = millis(); }

void Status::push(bool on, const char* closes) {
  value = on;
  strlcpy(until, closes ? closes : "", sizeof until);
  ok = true;
  lastOk = pushedAt = millis() | 1;
  failures = 0;
  strlcpy(error, "pushed", sizeof error);
}

void Status::loop() {
  if (!settings.statusEnabled || WiFi.status() != WL_CONNECTED) return;
  const uint32_t now = millis();
  if ((int32_t)(now - nextPoll) < 0) return;

  // Someone is pushing the value; no need to fetch it.
  if (pushedAt && now - pushedAt < 2 * pollIntervalMs()) {
    nextPoll = now + pollIntervalMs();
    return;
  }

  // Never interrupt a queued message. While idle, wait for a still frame (the
  // clock, a solid fill) so the pause cannot be seen; if none comes along
  // within two minutes, take the start of the next idle entry instead.
  if (!player.idleNow()) return;
  const bool overdue = now - nextPoll > 120000;
  if (!player.stillFrame() && !(overdue && player.justStarted())) return;

  poll();
  if (strcmp(error, "ok") == 0) {
    nextPoll = millis() + pollIntervalMs();
  } else {
    // Back off: 30 s, 60 s, 2 min ... up to the normal interval.
    failures++;
    uint32_t wait = 30000UL << (failures > 4 ? 4 : failures - 1);
    nextPoll = millis() + (wait < pollIntervalMs() ? wait : pollIntervalMs());
  }
}

// A value nobody has confirmed for half an hour is not shown.
static bool fresh() { return ok && millis() - lastOk < 30UL * 60 * 1000; }

bool Status::on() { return fresh() && value; }
bool Status::valid() { return fresh(); }
uint32_t Status::lastPollS() { return ok ? (millis() - lastOk) / 1000 : 0; }
uint32_t Status::lastPollMs() { return pollMs; }
const char* Status::lastError() { return error; }

const char* Status::untilRaw() { return until; }

int Status::untilCards(char* a, char* b, size_t n) {
  if (!on() || strlen(until) != 5 || until[2] != ':') return 0;
  int h = atoi(until), m = atoi(until + 3);
  if (h < 0 || h > 23 || m < 0 || m > 59) return 0;

  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_hour * 60 + tm.tm_min >= h * 60 + m) return 0;  // already past
  }
  const char* ap = h >= 12 ? "PM" : "AM";
  int h12 = h % 12 ? h % 12 : 12;
  if (m == 0) {
    snprintf(a, n, "%d %s", h12, ap);
    return 1;
  }
  snprintf(a, n, "%d:%02d", h12, m);
  snprintf(b, n, "%s", ap);
  return 2;
}
