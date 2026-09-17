#include "commands.h"
#include "effects.h"
#include "matrix.h"
#include "player.h"
#include "settings.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace {

struct Named { const char* name; uint32_t rgb; };
const Named kColors[] = {
    {"black", 0x000000},  {"off", 0x000000},     {"white", 0xFFFFFF},
    {"red", 0xFF0000},    {"green", 0x00FF00},   {"blue", 0x0000FF},
    {"yellow", 0xFFCC00}, {"orange", 0xFF6000},  {"cyan", 0x00FFFF},
    {"magenta", 0xFF00FF},{"pink", 0xFF3080},    {"purple", 0x8000FF},
    {"lime", 0x80FF00},   {"amber", 0xFFA000},   {"teal", 0x00FF80},
};

uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0xFF;
}

}  // namespace

uint32_t Commands::parseColor(const char* s, bool* rainbow) {
  if (rainbow) *rainbow = false;
  if (!s || !*s) return 0xFFFFFF;
  if (strcasecmp(s, "rainbow") == 0) {
    if (rainbow) *rainbow = true;
    return 0xFFFFFF;
  }
  for (const Named& c : kColors)
    if (strcasecmp(s, c.name) == 0) return c.rgb;

  const char* p = s;
  if (*p == '#') p++;
  else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

  uint32_t v = 0;
  int n = 0;
  for (; p[n] && n < 6; n++) {
    uint8_t d = hexVal(p[n]);
    if (d == 0xFF) return 0xFFFFFF;
    v = (v << 4) | d;
  }
  if (n == 3) {  // #abc
    uint32_t r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
    return (r * 17) << 16 | (g * 17) << 8 | (b * 17);
  }
  return n == 6 ? v : 0xFFFFFF;
}

static uint32_t colorField(JsonVariantConst v, uint32_t fallback, bool* rainbow) {
  if (v.isNull()) return fallback;
  if (v.is<const char*>()) return Commands::parseColor(v.as<const char*>(), rainbow);
  if (v.is<uint32_t>()) return v.as<uint32_t>() & 0xFFFFFF;
  return fallback;
}

bool Commands::apply(JsonObjectConst cmd, String& err) {
  // A batch runs each element in order.
  if (cmd["queue"].is<JsonArrayConst>()) {
    for (JsonObjectConst sub : cmd["queue"].as<JsonArrayConst>())
      if (!apply(sub, err)) return false;
    return true;
  }

  bool dirty = false;

  if (cmd["clear"].is<bool>() && cmd["clear"]) {
    player.clearQueue();
    player.skip();
  }
  if (cmd["skip"].is<bool>() && cmd["skip"]) player.skip();

  if (cmd["brightness"].is<int>()) {
    int b = cmd["brightness"].as<int>();
    settings.brightness = b < 0 ? 0 : (b > 255 ? 255 : b);
    dirty = true;
  }
  if (cmd["power_ma"].is<int>()) {
    settings.powerMa = cmd["power_ma"].as<int>();
    dirty = true;
  }
  if (cmd["idle"].is<const char*>()) {
    const char* name = cmd["idle"].as<const char*>();
    int fx = Effects::find(name);
    if (fx < 0) { err = "unknown idle effect"; return false; }
    Item it;
    it.kind = Item::Effect;
    it.effect = fx;
    it.color = colorField(cmd["color"], 0xFFFFFF, nullptr);
    strlcpy(settings.idle, name, sizeof settings.idle);
    player.setIdle(it);
    dirty = true;
  }

  // Build an item, if this command asks for one.
  Item it;
  const char* text = cmd["text"].as<const char*>();
  if (!text) text = cmd["say"].as<const char*>();
  const char* anim = cmd["anim"].as<const char*>();
  const char* play = cmd["play"].as<const char*>();

  if (text) {
    it.kind = Item::Text;
    strlcpy(it.arg, text, sizeof it.arg);
  } else if (anim) {
    it.kind = Item::Effect;
    it.effect = Effects::find(anim);
    if (it.effect < 0) { err = "unknown animation"; return false; }
  } else if (play) {
    it.kind = Item::Stored;
    strlcpy(it.arg, play, sizeof it.arg);
    if (!LittleFS.exists(String("/anim/") + it.arg)) {
      err = "no such stored animation";
      return false;
    }
  } else {
    if (dirty) settings.markDirty(cmd["save"] | false);
    return true;
  }

  bool rainbow = false;
  it.color   = colorField(cmd["color"], it.color, &rainbow);
  it.rainbow = cmd["rainbow"] | rainbow;
  it.scroll  = cmd["scroll"] | true;
  it.speed   = cmd["speed"] | it.speed;
  it.repeat  = cmd["repeat"] | 1;
  it.hold    = cmd["hold"] | 0U;
  it.fps     = cmd["fps"] | 0;

  if (cmd["now"] | false) player.play(it);
  else if (!player.enqueue(it)) { err = "queue full"; return false; }

  if (dirty) settings.markDirty(cmd["save"] | false);
  return true;
}

bool Commands::applyJson(const String& json, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = e.c_str(); return false; }
  if (doc.is<JsonArrayConst>()) {
    for (JsonObjectConst o : doc.as<JsonArrayConst>())
      if (!apply(o, err)) return false;
    return true;
  }
  if (!doc.is<JsonObjectConst>()) { err = "expected a JSON object"; return false; }
  return apply(doc.as<JsonObjectConst>(), err);
}

void Commands::state(JsonDocument& out) {
  const Item& c = player.current();
  out["host"]       = settings.host;
  out["ip"]         = WiFi.localIP().toString();
  out["rssi"]       = WiFi.RSSI();
  out["uptime_s"]   = millis() / 1000;
  out["free_heap"]  = ESP.getFreeHeap();
  out["width"]      = Matrix::W;
  out["height"]     = Matrix::H;
  out["brightness"] = settings.brightness;
  out["power_ma"]   = settings.powerMa;
  out["draw_ma"]    = matrix.lastDrawMa();
  out["idle"]       = settings.idle;
  out["queued"]     = player.queued();
  out["playing"]    = player.idleNow() ? "idle" : "item";

  JsonObject cur = out["current"].to<JsonObject>();
  cur["kind"] = c.kind == Item::Text ? "text" : (c.kind == Item::Stored ? "stored" : "anim");
  cur["arg"]  = c.kind == Item::Effect ? Effects::name(c.effect) : c.arg;

  JsonArray fx = out["animations"].to<JsonArray>();
  for (int i = 0; i < Effects::count(); i++) fx.add(Effects::name(i));

  JsonArray st = out["stored"].to<JsonArray>();
  Dir dir = LittleFS.openDir("/anim");
  while (dir.next()) st.add(dir.fileName());

  JsonObject cl = out["clasp"].to<JsonObject>();
  cl["enabled"] = settings.claspEnabled;
  cl["url"]     = settings.claspUrl;
  cl["device"]  = settings.claspDevice;
}
