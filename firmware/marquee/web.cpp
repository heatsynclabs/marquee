#include "web.h"
#include "commands.h"
#include "config.h"
#include "matrix.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "status.h"
#include "ui.h"
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <WiFiUdp.h>

namespace {

ESP8266WebServer  http(HTTP_PORT);
WebSocketsServer  ws(WS_PORT);
WiFiUDP           udp;
uint8_t           packet[Matrix::N * 3 + 8];

const size_t kFrameBytes = Matrix::N * 3;

void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  http.send(code, "application/json", out);
}

void sendOk() { http.send(200, "application/json", "{\"ok\":true}"); }

void sendErr(int code, const String& msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(code, doc);
}

// Accepts base64 or hex. Which one is decided by length, not by the characters
// used: a frame of zeroes encodes to "AAAA..." in base64, which is also valid
// hex. Returns the number of bytes written.
size_t decodeFrame(const String& in, uint8_t* out, size_t cap, size_t expect) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  size_t n = in.length();
  while (n && (in[n - 1] == '\n' || in[n - 1] == '\r' || in[n - 1] == '=')) n--;

  bool isHex = (n == expect * 2);
  for (size_t i = 0; i < n && isHex; i++) isHex = hex(in[i]) >= 0;

  if (isHex) {
    if (expect > cap) return 0;
    for (size_t i = 0; i < expect; i++)
      out[i] = (hex(in[2 * i]) << 4) | hex(in[2 * i + 1]);
    return expect;
  }

  static const char* kAlpha =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t acc = 0;
  int bits = 0;
  size_t w = 0;
  for (size_t i = 0; i < n; i++) {
    const char* p = strchr(kAlpha, in[i]);
    if (!p || !in[i]) continue;
    acc = (acc << 6) | (p - kAlpha);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (w >= cap) return 0;
      out[w++] = (acc >> bits) & 0xFF;
    }
  }
  return w;
}

// The Arduino String has no buffer+length constructor.
String toString(const uint8_t* p, size_t n) {
  String s;
  s.reserve(n + 1);
  for (size_t i = 0; i < n; i++) s += (char)p[i];
  return s;
}

// curl -d sends a form content type unless told otherwise, and the server then
// parses the JSON body as a parameter name. Accept it either way.
String bodyJson() {
  if (http.hasArg("plain") && http.arg("plain").length()) return http.arg("plain");
  if (http.args() == 1 && http.argName(0).startsWith("{")) return http.argName(0);
  return String();
}

String animPath(const String& name) { return "/anim/" + name; }

bool validName(const String& n) {
  if (!n.length() || n.length() > 24) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    bool ok = isalnum(c) || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

void handleState() {
  JsonDocument doc;
  Commands::state(doc);
  sendJson(200, doc);
}

// Commands arrive either as a JSON body or as query parameters, so that both
// a script and a plain browser URL can drive the panel.
void handleCmd() {
  String err;
  bool ok;
  String body = bodyJson();
  if (body.length()) {
    ok = Commands::applyJson(body, err);
  } else {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    for (int i = 0; i < http.args(); i++) {
      String k = http.argName(i), v = http.arg(i);
      if (k == "plain") continue;
      if (v == "true") o[k] = true;
      else if (v == "false") o[k] = false;
      else if (v.length() && (isdigit(v[0]) || v[0] == '-') && String(v.toInt()) == v) o[k] = v.toInt();
      else o[k] = v;
    }
    ok = Commands::apply(o, err);
  }
  if (!ok) return sendErr(400, err);
  sendOk();
}

void handleConfigGet() {
  JsonDocument doc;
  doc["host"]          = settings.host;
  doc["ssid"]          = settings.ssid;
  doc["brightness"]    = settings.brightness;
  doc["power_ma"]      = settings.powerMa;
  doc["tz"]            = settings.tz;
  doc["status_enabled"]= settings.statusEnabled;
  doc["status_url"]    = settings.statusUrl;
  doc["status_key"]    = settings.statusKey;
  doc["status_poll_s"] = settings.statusPollS;
  doc["status_every"]  = settings.statusEvery;
  doc["clasp_enabled"] = settings.claspEnabled;
  doc["clasp_url"]     = settings.claspUrl;
  doc["clasp_device"]  = settings.claspDevice;
  sendJson(200, doc);
}

void handleConfigPost() {
  JsonDocument doc;
  if (deserializeJson(doc, bodyJson())) return sendErr(400, "bad JSON");

  auto str = [&](const char* key, char* dst, size_t n) {
    if (doc[key].is<const char*>()) strlcpy(dst, doc[key].as<const char*>(), n);
  };
  str("host", settings.host, sizeof settings.host);
  str("ssid", settings.ssid, sizeof settings.ssid);
  str("pass", settings.pass, sizeof settings.pass);
  str("tz", settings.tz, sizeof settings.tz);
  str("status_url", settings.statusUrl, sizeof settings.statusUrl);
  str("status_key", settings.statusKey, sizeof settings.statusKey);
  str("clasp_url", settings.claspUrl, sizeof settings.claspUrl);
  str("clasp_device", settings.claspDevice, sizeof settings.claspDevice);
  if (doc["brightness"].is<int>()) settings.brightness = doc["brightness"].as<int>();
  if (doc["power_ma"].is<int>()) settings.powerMa = doc["power_ma"].as<int>();
  if (doc["clasp_enabled"].is<bool>()) settings.claspEnabled = doc["clasp_enabled"];
  if (doc["status_enabled"].is<bool>()) settings.statusEnabled = doc["status_enabled"];
  if (doc["status_poll_s"].is<int>()) settings.statusPollS = doc["status_poll_s"].as<int>();
  if (doc["status_every"].is<int>()) settings.statusEvery = doc["status_every"].as<int>();
  if (doc["status_url"].is<const char*>() || doc["status_key"].is<const char*>()) Status::pollNow();
  settings.save();

  bool reboot = doc["reboot"] | false;
  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = reboot;
  sendJson(200, res);
  if (reboot) {
    delay(200);
    ESP.restart();
  }
}

void handleRotateGet() {
  JsonDocument doc;
  Commands::rotationJson(doc);
  sendJson(200, doc);
}

// POST /api/rotate replaces the idle rotation: {"ms":15000,"items":[...],"open":{...}}
void handleRotatePost() {
  JsonDocument doc;
  if (deserializeJson(doc, bodyJson())) return sendErr(400, "bad JSON");
  String err;
  if (!Commands::applyRotation(doc.as<JsonObjectConst>(), err)) return sendErr(400, err);
  sendOk();
}

void handleStatusPoll() {
  Status::pollNow();
  sendOk();
}

void handleAnimList() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  Dir dir = LittleFS.openDir("/anim");
  while (dir.next()) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = dir.fileName();
    size_t size = dir.fileSize();
    o["frames"] = size > 10 ? (size - 10) / kFrameBytes : 0;
  }
  sendJson(200, doc);
}

// POST /api/anim?name=x&fps=12 starts a new animation file.
void handleAnimNew() {
  String name = http.arg("name");
  if (!validName(name)) return sendErr(400, "bad name");
  uint16_t fps = http.hasArg("fps") ? http.arg("fps").toInt() : 12;
  LittleFS.mkdir("/anim");
  File f = LittleFS.open(animPath(name), "w");
  if (!f) return sendErr(500, "cannot create file");
  uint8_t hdr[10] = {'M', 'Q', 'A', '1',
                     (uint8_t)(Matrix::W >> 8), (uint8_t)Matrix::W,
                     (uint8_t)(Matrix::H >> 8), (uint8_t)Matrix::H,
                     (uint8_t)(fps >> 8), (uint8_t)fps};
  f.write(hdr, sizeof hdr);
  f.close();
  sendOk();
}

// POST /api/anim/frame?name=x with one frame of pixels in the body.
void handleAnimFrame() {
  String name = http.arg("name");
  if (!validName(name) || !LittleFS.exists(animPath(name)))
    return sendErr(404, "unknown animation");

  size_t n = decodeFrame(http.arg("plain"), packet, sizeof packet, kFrameBytes);
  if (n != kFrameBytes) return sendErr(400, String("expected ") + kFrameBytes + " bytes, got " + n);

  File f = LittleFS.open(animPath(name), "a");
  if (!f) return sendErr(500, "cannot append");
  f.write(packet, n);
  f.close();
  sendOk();
}

void handleAnimDelete() {
  String name = http.arg("name");
  if (!validName(name)) return sendErr(400, "bad name");
  LittleFS.remove(animPath(name));
  sendOk();
}

void handleWifiSave() {
  strlcpy(settings.ssid, http.arg("ssid").c_str(), sizeof settings.ssid);
  strlcpy(settings.pass, http.arg("pass").c_str(), sizeof settings.pass);
  if (http.hasArg("host") && http.arg("host").length())
    strlcpy(settings.host, http.arg("host").c_str(), sizeof settings.host);
  settings.save();
  http.send(200, "text/html", "<p>Saved. Rebooting.</p>");
  delay(300);
  ESP.restart();
}

void handleRoot() {
  if (Net::apMode()) return http.send_P(200, "text/html", SETUP_HTML);
  http.send_P(200, "text/html", INDEX_HTML);
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_TEXT) {
    String err, body = toString(payload, len);
    if (!Commands::applyJson(body, err)) {
      String reply = String("{\"ok\":false,\"error\":\"") + err + "\"}";
      ws.sendTXT(num, reply);
    }
  } else if (type == WStype_BIN && len == kFrameBytes) {
    matrix.blit(payload, len);
    player.realtimeFrame(2000);
  }
}

// WLED-compatible realtime UDP, plus plain JSON commands on the same port.
void handleUdp() {
  int size = udp.parsePacket();
  if (size <= 0) return;
  int n = udp.read(packet, sizeof packet);
  if (n <= 0) return;

  if (packet[0] == '{') {
    String err, body = toString(packet, n);
    Commands::applyJson(body, err);
    return;
  }
  if (n < 2) return;

  const uint8_t proto = packet[0];
  const uint32_t timeout = (packet[1] ? packet[1] : 2) * 1000UL;
  const uint8_t* d = packet + 2;
  int avail = n - 2;

  if (proto == 1) {  // WARLS: index, r, g, b
    for (int i = 0; i + 3 < avail; i += 4)
      matrix.blit(d + i + 1, 3, d[i]);
  } else if (proto == 2) {  // DRGB: pixels from zero
    matrix.blit(d, avail);
  } else if (proto == 4) {  // DNRGB: u16 start index, then pixels
    if (avail < 2) return;
    uint16_t start = (d[0] << 8) | d[1];
    matrix.blit(d + 2, avail - 2, start);
  } else {
    return;
  }
  player.realtimeFrame(timeout);
}

}  // namespace

void Web::begin() {
  http.on("/", handleRoot);
  http.on("/api/state", handleState);
  http.on("/api/cmd", handleCmd);
  http.on("/api/config", HTTP_GET, handleConfigGet);
  http.on("/api/config", HTTP_POST, handleConfigPost);
  http.on("/api/rotate", HTTP_GET, handleRotateGet);
  http.on("/api/rotate", HTTP_POST, handleRotatePost);
  http.on("/api/status/poll", HTTP_POST, handleStatusPoll);
  http.on("/api/anim", HTTP_GET, handleAnimList);
  http.on("/api/anim", HTTP_POST, handleAnimNew);
  http.on("/api/anim", HTTP_DELETE, handleAnimDelete);
  http.on("/api/anim/frame", HTTP_POST, handleAnimFrame);
  http.on("/api/wifi", HTTP_POST, handleWifiSave);
  http.on("/api/reboot", HTTP_POST, []() {
    sendOk();
    delay(200);
    ESP.restart();
  });
  http.onNotFound([]() {
    if (Net::apMode()) return handleRoot();  // captive portal
    sendErr(404, "not found");
  });
  http.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  udp.begin(UDP_PORT);
}

void Web::loop() {
  http.handleClient();
  ws.loop();
  handleUdp();
}
