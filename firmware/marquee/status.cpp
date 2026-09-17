#include "status.h"
#include "config.h"
#include "settings.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "player.h"

namespace {

bool     value = false;
bool     ok = false;
uint32_t lastOk = 0;
uint32_t nextPoll = 0;
uint32_t pollMs = 0;
char     error[40] = "not polled yet";

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

// Blocks for a second or so. Status::loop only calls it while the panel is
// idle, so a queued message is never interrupted; the idle frame just holds.
void poll() {
  uint32_t t0 = millis();
  bool tls;
  char host[80], path[96];
  uint16_t port;
  if (!splitUrl(settings.statusUrl, tls, host, sizeof host, port, path, sizeof path))
    return fail("bad status url");

  const uint32_t need = tls ? STATUS_TLS_BUFFER + 12000 : 4000;
  if (ESP.getFreeHeap() < need) return fail("low heap, skipped");

  WiFiClient* client;
  if (tls) {
    BearSSL::WiFiClientSecure* s = new BearSSL::WiFiClientSecure();
    s->setInsecure();
    s->setBufferSizes(STATUS_TLS_BUFFER, 512);
    client = s;
  } else {
    client = new WiFiClient();
  }
  client->setTimeout(8000);

  if (!client->connect(host, port)) {
    delete client;
    return fail("connect failed");
  }
  client->printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                 "User-Agent: marquee\r\n\r\n", path, host);

  KeyScanner scan(settings.statusKey);
  char found = 0;
  uint32_t deadline = millis() + 10000;
  while ((client->connected() || client->available()) && millis() < deadline && !found) {
    while (client->available()) {
      char c = (char)client->read();
      found = scan.feed(c);
      if (found) break;
    }
    if (!found) delay(1);
  }
  client->stop();
  delete client;

  if (!found) return fail("key not found");

  pollMs = millis() - t0;
  value = (found == 't');
  ok = true;
  lastOk = millis();
  strlcpy(error, "ok", sizeof error);
  Serial.printf("status: %s = %s (%lu ms)\n", settings.statusKey,
                value ? "true" : "false", (unsigned long)(millis() - t0));
}

}  // namespace

void Status::begin() { nextPoll = millis() + 5000; }

void Status::pollNow() { nextPoll = millis(); }

void Status::loop() {
  if (!settings.statusEnabled || WiFi.status() != WL_CONNECTED) return;
  if ((int32_t)(millis() - nextPoll) < 0) return;
  // Never interrupt a queued message; the pause belongs to the idle loop.
  if (!player.idleNow()) return;
  nextPoll = millis() + (uint32_t)(settings.statusPollS ? settings.statusPollS : 300) * 1000;
  poll();
}

bool Status::on() { return ok && value; }
bool Status::valid() { return ok; }
uint32_t Status::lastPollS() { return ok ? (millis() - lastOk) / 1000 : 0; }
uint32_t Status::lastPollMs() { return pollMs; }
const char* Status::lastError() { return error; }
