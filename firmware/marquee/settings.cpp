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
  copyStr(tz, sizeof tz, DEFAULT_TZ);
  statusEnabled = true;
  copyStr(statusUrl, sizeof statusUrl, DEFAULT_STATUS_URL);
  copyStr(statusKey, sizeof statusKey, DEFAULT_STATUS_KEY);
  statusPollS = DEFAULT_STATUS_POLL;
  statusEvery = DEFAULT_STATUS_EVERY;
  claspEnabled = false;
  copyStr(claspUrl, sizeof claspUrl, "wss://relay.clasp.to");
  copyStr(claspDevice, sizeof claspDevice, "");
  copyStr(staticIp, sizeof staticIp, "");
  copyStr(staticGw, sizeof staticGw, "");
  copyStr(staticMask, sizeof staticMask, "");
  copyStr(staticDns, sizeof staticDns, "");
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

  // A stored network wins, but an empty one falls back to the build defaults
  // so that setting WIFI_SSID still works on a board that has booted before.
  const char* savedSsid = doc["ssid"] | "";
  if (savedSsid[0]) {
    copyStr(ssid, sizeof ssid, savedSsid);
    copyStr(pass, sizeof pass, doc["pass"] | "");
  }
  brightness = doc["brightness"] | brightness;
  powerMa    = doc["power_ma"] | powerMa;
  copyStr(tz, sizeof tz, doc["tz"] | tz);
  statusEnabled = doc["status_enabled"] | statusEnabled;
  copyStr(statusUrl, sizeof statusUrl, doc["status_url"] | statusUrl);
  copyStr(statusKey, sizeof statusKey, doc["status_key"] | statusKey);
  statusPollS = doc["status_poll_s"] | statusPollS;
  statusEvery = doc["status_every"] | statusEvery;
  claspEnabled = doc["clasp_enabled"] | claspEnabled;
  copyStr(claspUrl, sizeof claspUrl, doc["clasp_url"] | claspUrl);
  copyStr(claspDevice, sizeof claspDevice, doc["clasp_device"] | claspDevice);
  copyStr(staticIp, sizeof staticIp, doc["static_ip"] | staticIp);
  copyStr(staticGw, sizeof staticGw, doc["static_gw"] | staticGw);
  copyStr(staticMask, sizeof staticMask, doc["static_mask"] | staticMask);
  copyStr(staticDns, sizeof staticDns, doc["static_dns"] | staticDns);
  return true;
}

bool Settings::save() {
  JsonDocument doc;
  doc["host"]          = host;
  doc["ssid"]          = ssid;
  doc["pass"]          = pass;
  doc["brightness"]    = brightness;
  doc["power_ma"]      = powerMa;
  doc["tz"]            = tz;
  doc["status_enabled"]= statusEnabled;
  doc["status_url"]    = statusUrl;
  doc["status_key"]    = statusKey;
  doc["status_poll_s"] = statusPollS;
  doc["status_every"]  = statusEvery;
  doc["clasp_enabled"] = claspEnabled;
  doc["clasp_url"]     = claspUrl;
  doc["clasp_device"]  = claspDevice;
  doc["static_ip"]     = staticIp;
  doc["static_gw"]     = staticGw;
  doc["static_mask"]   = staticMask;
  doc["static_dns"]    = staticDns;

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
