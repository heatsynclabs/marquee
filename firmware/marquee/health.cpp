#include "health.h"
#include "config.h"
#include "player.h"

namespace {
char     reason[32] = "";
uint32_t passStart = 0;
uint32_t maxThis = 0, maxLast = 0, windowAt = 0;
uint32_t lowSince = 0;
}  // namespace

void Health::begin() {
  strlcpy(reason, ESP.getResetReason().c_str(), sizeof reason);
  Serial.printf("reset reason: %s\n", reason);
}

void Health::loopStart() { passStart = millis(); }

void Health::loopEnd() {
  const uint32_t now = millis();
  const uint32_t pass = now - passStart;
  if (pass > maxThis) maxThis = pass;
  if (now - windowAt >= 60000) {
    windowAt = now;
    maxLast = maxThis;
    maxThis = 0;
  }

  // A heap that stays this fragmented only gets worse. Restart during an
  // idle moment rather than wait for an allocation to fail somewhere.
  if (ESP.getMaxFreeBlockSize() < HEAP_LOW_BLOCK) {
    if (!lowSince) lowSince = now | 1;
    if (now - lowSince > HEAP_LOW_MS && player.idleNow()) {
      Serial.println("heap too fragmented; restarting");
      delay(50);
      ESP.restart();
    }
  } else {
    lowSince = 0;
  }
}

const char* Health::resetReason() { return reason; }
uint32_t Health::loopMaxMs() { return maxThis > maxLast ? maxThis : maxLast; }
