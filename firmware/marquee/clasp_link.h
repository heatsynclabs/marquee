// Optional link to a CLASP relay, so the panel can be driven from outside the
// local network. See docs/clasp.md.
//
// This is a small purpose-built WebSocket client rather than a library: the
// public relay greets every client with a full state snapshot that can run to
// tens of kilobytes, which no ESP8266 can buffer. Frames larger than the
// receive buffer are read and discarded a chunk at a time instead.
#pragma once
#include <Arduino.h>

namespace ClaspLink {
void begin();
void loop();
bool connected();
const char* deviceId();
}  // namespace ClaspLink
