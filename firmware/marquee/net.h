// Wi-Fi, the setup portal, mDNS, OTA and NTP.
#pragma once
#include <Arduino.h>

namespace Net {
void begin();
void loop();
void applyTime();      // (re)apply settings.tz and the NTP servers
bool apMode();         // setup portal up and no network: serve the setup page
bool portalOpen();
bool online();
uint32_t reconnects(); // times the station dropped since boot
String ip();
}  // namespace Net
