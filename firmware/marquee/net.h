// Wi-Fi, the first-boot setup portal, mDNS and OTA.
#pragma once
#include <Arduino.h>

namespace Net {
void begin();
void loop();
bool apMode();     // true while the setup portal is up
bool online();
String ip();
}  // namespace Net
