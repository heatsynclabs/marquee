// Polls a JSON endpoint for one boolean and remembers the answer.
//
// Out of the box it watches whether HeatSync Labs is open, using the same
// SpaceAPI document the lab's website reads. Point it at any URL that returns
// a JSON boolean and it will work the same way.
#pragma once
#include <Arduino.h>

namespace Status {
void begin();
void loop();
void pollNow();

bool     on();          // last known value of the watched key
bool     valid();       // true once a poll has succeeded
uint32_t lastPollS();   // seconds since the last successful poll
uint32_t lastPollMs();  // how long that poll took
const char* lastError();
}  // namespace Status
