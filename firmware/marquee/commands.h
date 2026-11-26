// The single command interpreter. HTTP, WebSocket and the CLASP relay all
// funnel into apply(); see docs/api.md for the field list.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "player.h"

namespace Commands {
bool     apply(JsonObjectConst cmd, String& err);
bool     applyJson(const char* json, size_t len, String& err);  // object or array
inline bool applyJson(const String& json, String& err) {
  return applyJson(json.c_str(), json.length(), err);
}
void     state(JsonDocument& out);
uint32_t parseColor(const char* s, bool* rainbow = nullptr);

// 1 when an item was built, 0 when the command asks for none, -1 on error.
int      buildItem(JsonObjectConst cmd, Item& it, String& err);

// The idle rotation, stored in /rotate.json.
void     loadRotation();
void     saveRotation();
bool     setRotation(JsonVariantConst spec, String& err, bool persist);
bool     setStatusItem(JsonVariantConst spec, String& err);
bool     applyRotation(JsonObjectConst spec, String& err);
void     rotationJson(JsonDocument& out);
}  // namespace Commands
