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
// The value (and optional "HH:MM" closing time), delivered by someone else
// such as the members site, instead of polled.
void push(bool on, const char* closes = nullptr);

bool     on();          // last known value of the watched key
bool     valid();       // true once a poll has succeeded
uint32_t lastPollS();   // seconds since the last successful poll
uint32_t lastPollMs();  // how long that poll took
const char* lastError();

// Closing time from the feed's optional "open_until" ("HH:MM", local), as one
// or two cards that fit the panel: "9 PM", or "9:30" + "PM". Returns how many
// were written, 0 when there is no closing time or it has passed.
int untilCards(char* a, char* b, size_t n);
const char* untilRaw();
}  // namespace Status
