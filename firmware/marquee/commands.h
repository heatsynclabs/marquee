// The single command interpreter. HTTP, WebSocket and the CLASP relay all
// funnel into apply(); see docs/api.md for the field list.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Commands {
bool     apply(JsonObjectConst cmd, String& err);
bool     applyJson(const String& json, String& err);  // object or array
void     state(JsonDocument& out);
uint32_t parseColor(const char* s, bool* rainbow = nullptr);
}  // namespace Commands
