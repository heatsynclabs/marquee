// Keeps the panel honest about how it is doing, and restarts it cleanly when
// the heap is too fragmented to recover. Reported in /api/state.
#pragma once
#include <Arduino.h>

namespace Health {
void begin();
void loopStart();
void loopEnd();
const char* resetReason();  // why the last boot happened
uint32_t loopMaxMs();       // longest loop pass in the last minute
}  // namespace Health
