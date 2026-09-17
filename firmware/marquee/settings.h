// Persistent settings, stored as JSON in LittleFS at /config.json.
#pragma once
#include <Arduino.h>

struct Settings {
  char     host[32];
  char     ssid[33];
  char     pass[65];
  uint8_t  brightness;
  uint16_t powerMa;
  char     idle[48];      // effect name shown when the queue drains
  char     tz[48];        // POSIX TZ string for the clock effect
  bool     claspEnabled;
  char     claspUrl[96];  // ws:// or wss:// relay URL
  char     claspDevice[32];

  void defaults();
  bool load();
  bool save();

  // Settings changed; write them out shortly, or right away when now is set.
  void markDirty(bool now = false);
  void tick();

 private:
  uint32_t dirtyAt_ = 0;
};

extern Settings settings;
