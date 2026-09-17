#include "settings.h"
#include "config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

Settings settings;

static const char* kPath = "/config.json";

static void copyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

void Settings::defaults() {
  copyStr(host, sizeof host, DEFAULT_HOSTNAME);
  copyStr(ssid, sizeof ssid, WIFI_SSID);
  copyStr(pass, sizeof pass, WIFI_PASS);
  brightness = DEFAULT_BRIGHTNESS;
  powerMa    = DEFAULT_POWER_MA;
  copyStr(idle, sizeof idle, DEFAULT_IDLE);
  copyStr(tz, sizeof tz, DEFAULT_TZ);
  claspEnabled = false;
  copyStr(claspUrl, sizeof claspUrl, "wss://relay.clasp.to");
  copyStr(claspDevice, sizeof claspDevice, "");
}

bool Settings::load() {
  defaults();
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  copyStr(host, sizeof host, doc["host"] | host);
  copyStr(ssid, sizeof ssid, doc["ssid"] | ssid);
  copyStr(pass, sizeof pass, doc["pass"] | pass);
  brightness = doc["brightness"] | brightness;
  powerMa    = doc["power_ma"] | powerMa;
  copyStr(idle, sizeof idle, doc["idle"] | idle);
  copyStr(tz, sizeof tz, doc["tz"] | tz);
  claspEnabled = doc["clasp_enabled"] | claspEnabled;
  copyStr(claspUrl, sizeof claspUrl, doc["clasp_url"] | claspUrl);
  copyStr(claspDevice, sizeof claspDevice, doc["clasp_device"] | claspDevice);
  return true;
}

bool Settings::save() {
  JsonDocument doc;
  doc["host"]          = host;
  doc["ssid"]          = ssid;
  doc["pass"]          = pass;
  doc["brightness"]    = brightness;
  doc["power_ma"]      = powerMa;
  doc["idle"]          = idle;
  doc["tz"]            = tz;
  doc["clasp_enabled"] = claspEnabled;
  doc["clasp_url"]     = claspUrl;
  doc["clasp_device"]  = claspDevice;

  File f = LittleFS.open(kPath, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

// Flash has a finite write budget, so a burst of changes (a brightness slider,
// say) collapses into one write a second after the last one.
void Settings::markDirty(bool now) {
  if (now) { dirtyAt_ = 0; save(); return; }
  dirtyAt_ = millis() | 1;
}

void Settings::tick() {
  if (dirtyAt_ && millis() - dirtyAt_ > 1000) {
    dirtyAt_ = 0;
    save();
  }
}
