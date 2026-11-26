// Persistent settings, stored as JSON in LittleFS at /config.json.
#pragma once
#include <Arduino.h>

struct Settings {
  char     host[32];
  char     ssid[33];
  char     pass[65];
  uint8_t  brightness;
  uint16_t powerMa;
  char     tz[48];        // POSIX TZ string for the clock effect
  bool     statusEnabled;
  char     statusUrl[112];
  char     statusKey[24];
  uint16_t statusPollS;
  uint8_t  statusEvery;
  bool     claspEnabled;
  char     claspUrl[96];  // ws:// or wss:// relay URL
  char     claspDevice[32];
  // Fixed address. Empty ip means DHCP. Gateway and DNS default to .1 of
  // the address's network when left empty.
  char     staticIp[16];
  char     staticGw[16];
  char     staticMask[16];
  char     staticDns[16];

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
