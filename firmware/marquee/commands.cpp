#include "commands.h"
#include "effects.h"
#include "matrix.h"
#include "player.h"
#include "settings.h"
#include "status.h"
#include "clasp_link.h"
#include "config.h"
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

int Commands::buildItem(JsonObjectConst cmd, Item& it, String& err) {
  const char* text = cmd["text"].as<const char*>();
  if (!text) text = cmd["say"].as<const char*>();
  const char* anim = cmd["anim"].as<const char*>();
  const char* play = cmd["play"].as<const char*>();

  if (text) {
    it.kind = Item::Text;
    strlcpy(it.arg, text, sizeof it.arg);
  } else if (anim) {
    it.kind = Item::Effect;
    int fx = Effects::find(anim);
    if (fx < 0) { err = "unknown animation"; return -1; }
    it.effect = fx;
  } else if (play) {
    it.kind = Item::Stored;
    strlcpy(it.arg, play, sizeof it.arg);
    if (!LittleFS.exists(String("/anim/") + it.arg)) {
      err = "no such stored animation";
      return -1;
    }
  } else {
    return 0;
  }

  bool rainbow = false;
  it.color   = colorField(cmd["color"], it.color, &rainbow);
  it.rainbow = cmd["rainbow"] | rainbow;
  it.scroll  = cmd["scroll"] | true;
  it.speed   = cmd["speed"] | it.speed;
  it.repeat  = cmd["repeat"] | 1;
  it.hold    = cmd["hold"] | 0U;
  it.fps     = cmd["fps"] | 0;
  return 1;
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
  if (!cmd["idle"].isNull()) {
    if (!setRotation(cmd["idle"], err, true)) return false;
  }
  if (cmd["rotate"].is<JsonObjectConst>()) {
    if (!applyRotation(cmd["rotate"].as<JsonObjectConst>(), err)) return false;
  }
  if (cmd["rotate_ms"].is<uint32_t>()) {
    player.setDwell(cmd["rotate_ms"].as<uint32_t>());
    saveRotation();
  }

  Item it;
  int built = buildItem(cmd, it, err);
  if (built < 0) return false;
  if (built == 0) {
    if (dirty) settings.markDirty(cmd["save"] | false);
    return true;
  }

  const bool now = cmd["now"] | false;

  // An effect with no time limit replaces the rotation rather than becoming a
  // queue entry, so that a message sent afterwards still gets its turn.
  if (it.kind == Item::Effect && it.hold == 0) {
    player.setRotation(it);
    if (now) {
      player.clearQueue();
      player.skip();
    }
  } else if (now) {
    player.play(it);
  } else if (!player.enqueue(it)) {
    err = "queue full";
    return false;
  }

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
  out["queued"]     = player.queued();
  out["playing"]    = player.idleNow() ? "idle" : "item";

  JsonObject st = out["status"].to<JsonObject>();
  st["enabled"]     = settings.statusEnabled;
  st["url"]         = settings.statusUrl;
  st["key"]         = settings.statusKey;
  st["on"]          = Status::on();
  st["valid"]       = Status::valid();
  st["every"]       = settings.statusEvery;
  st["age_s"]       = Status::lastPollS();
  st["poll_ms"]     = Status::lastPollMs();
  st["last"]        = Status::lastError();

  JsonObject cur = out["current"].to<JsonObject>();
  cur["kind"] = c.kind == Item::Text ? "text" : (c.kind == Item::Stored ? "stored" : "anim");
  cur["arg"]  = c.kind == Item::Effect ? Effects::name(c.effect) : c.arg;

  JsonDocument rot;
  rotationJson(rot);
  out["idle"] = rot;

  JsonArray fx = out["animations"].to<JsonArray>();
  for (int i = 0; i < Effects::count(); i++) fx.add(Effects::name(i));

  JsonArray stored = out["stored"].to<JsonArray>();
  Dir dir = LittleFS.openDir("/anim");
  while (dir.next()) stored.add(dir.fileName());

  JsonObject cl = out["clasp"].to<JsonObject>();
  cl["enabled"] = settings.claspEnabled;
  cl["url"]     = settings.claspUrl;
  cl["device"]  = settings.claspDevice;
  cl["connected"] = ClaspLink::connected();
  cl["last"]    = ClaspLink::lastNote();
}

// --- idle rotation ----------------------------------------------------------

static const char* kRotationPath = "/rotate.json";

static void liveRotation(JsonDocument& out);

static void itemJson(const Item& it, JsonObject o) {
  char hex[8];
  snprintf(hex, sizeof hex, "#%06lx", (unsigned long)(it.color & 0xFFFFFF));
  switch (it.kind) {
    case Item::Text:
      o["text"] = it.arg;
      // A char array is copied by ArduinoJson; a const char* would only be
      // linked, and hex is about to go out of scope.
      if (it.rainbow) o["color"] = "rainbow";
      else o["color"] = hex;
      if (it.speed != 20) o["speed"] = it.speed;
      if (it.repeat != 1) o["repeat"] = it.repeat;
      if (!it.scroll) o["scroll"] = false;
      break;
    case Item::Stored:
      o["play"] = it.arg;
      if (it.fps) o["fps"] = it.fps;
      break;
    default:
      o["anim"] = Effects::name(it.effect);
      if (it.color != 0xFFFFFF) o["color"] = hex;
      break;
  }
  if (it.hold) o["hold"] = it.hold;
}

// The rotation as the player is running it right now.
static void liveRotation(JsonDocument& out) {
  out["ms"] = player.dwellMs();
  JsonArray items = out["items"].to<JsonArray>();
  for (uint8_t i = 0; i < player.rotationCount(); i++)
    itemJson(player.rotationAt(i), items.add<JsonObject>());
  if (player.hasStatusItem())
    itemJson(player.statusItem(), out["open"].to<JsonObject>());
}

// What callers see: the saved configuration. A bare {"anim":...} swaps the
// live rotation without saving, so reading this back and posting it again
// never loses a rotation someone set on purpose.
void Commands::rotationJson(JsonDocument& out) {
  File f = LittleFS.open(kRotationPath, "r");
  bool have = f && !deserializeJson(out, f);
  if (f) f.close();
  if (!have) {
    out.clear();
    liveRotation(out);
  }
}

void Commands::saveRotation() {
  JsonDocument doc;
  liveRotation(doc);
  File f = LittleFS.open(kRotationPath, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// The spec is an effect name, one command object, or a list of them.
bool Commands::setRotation(JsonVariantConst spec, String& err, bool persist) {
  Item items[Player::kRotation];
  uint8_t n = 0;

  if (spec.is<const char*>()) {
    JsonDocument one;
    one["anim"] = spec.as<const char*>();
    if (buildItem(one.as<JsonObjectConst>(), items[0], err) != 1) {
      if (!err.length()) err = "unknown idle effect";
      return false;
    }
    n = 1;
  } else if (spec.is<JsonArrayConst>()) {
    for (JsonObjectConst o : spec.as<JsonArrayConst>()) {
      if (n >= Player::kRotation) break;
      Item it;
      int built = buildItem(o, it, err);
      if (built < 0) return false;
      if (built == 1) items[n++] = it;
    }
  } else if (spec.is<JsonObjectConst>()) {
    int built = buildItem(spec.as<JsonObjectConst>(), items[0], err);
    if (built < 0) return false;
    if (built == 1) n = 1;
  }

  if (!n) { err = "idle needs at least one entry"; return false; }
  player.setRotation(items, n, 0);
  if (persist) saveRotation();
  return true;
}

bool Commands::setStatusItem(JsonVariantConst spec, String& err) {
  if (spec.isNull() || spec.is<bool>()) {
    player.setStatusItem(Item(), false);
    saveRotation();
    return true;
  }
  Item it;
  if (buildItem(spec.as<JsonObjectConst>(), it, err) != 1) {
    if (!err.length()) err = "bad open item";
    return false;
  }
  player.setStatusItem(it, true);
  saveRotation();
  return true;
}

// Handles a whole rotation document: {"ms":..,"items":[..],"open":{..}}
bool Commands::applyRotation(JsonObjectConst spec, String& err) {
  if (spec["ms"].is<uint32_t>()) player.setDwell(spec["ms"].as<uint32_t>());
  if (!spec["items"].isNull() && !setRotation(spec["items"], err, false)) return false;
  if (spec["open"].is<JsonObjectConst>() || spec["open"].is<bool>()) {
    if (!setStatusItem(spec["open"], err)) return false;
  }
  saveRotation();
  return true;
}

void Commands::loadRotation() {
  JsonDocument doc;
  File f = LittleFS.open(kRotationPath, "r");
  bool have = f && !deserializeJson(doc, f);
  if (f) f.close();

  String err;
  if (have && doc["ms"].is<uint32_t>()) player.setDwell(doc["ms"].as<uint32_t>());
  if (!have || !setRotation(doc["items"], err, false)) {
    JsonDocument fallback;
    fallback["anim"] = "clock";
    setRotation(fallback.as<JsonVariantConst>(), err, false);
  }

  Item statusItem;
  bool ok = false;
  if (have && doc["open"].is<JsonObjectConst>()) {
    ok = buildItem(doc["open"].as<JsonObjectConst>(), statusItem, err) == 1;
  }
  if (!ok) {
    JsonDocument def;
    def["text"] = DEFAULT_STATUS_TEXT;
    def["color"] = DEFAULT_STATUS_COLOR;
    ok = buildItem(def.as<JsonObjectConst>(), statusItem, err) == 1;
  }
  player.setStatusItem(statusItem, ok);
}
