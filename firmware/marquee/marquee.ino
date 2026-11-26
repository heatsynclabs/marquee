// Marquee: a network-controlled LED matrix.
// https://github.com/heatsynclabs/marquee (see README.md)

#include <LittleFS.h>

#include "clasp_link.h"
#include "commands.h"
#include "config.h"
#include "effects.h"
#include "health.h"
#include "matrix.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "status.h"
#include "web.h"

static uint32_t lastFrame = 0;

// If the board resets three times in a row without reaching a steady state,
// turn the optional relay client off rather than looping forever. The counter
// lives in RTC memory, so it survives a crash but not a power cycle.
static const uint32_t kGuardMagic = 0x4d515545;
static const uint32_t kGuardSlot  = 64;
static const uint32_t kSettleMs   = 20000;
static bool guardArmed = false;

struct Guard {
  uint32_t magic;
  uint32_t boots;
};

static void guardStart() {
  Guard g;
  ESP.rtcUserMemoryRead(kGuardSlot, (uint32_t*)&g, sizeof g);
  g.boots = (g.magic == kGuardMagic) ? g.boots + 1 : 1;
  g.magic = kGuardMagic;
  ESP.rtcUserMemoryWrite(kGuardSlot, (uint32_t*)&g, sizeof g);
  guardArmed = true;

  if (g.boots >= 3 && settings.claspEnabled) {
    Serial.printf("guard: %u quick restarts, disabling the clasp relay\n", g.boots);
    settings.claspEnabled = false;
    settings.save();
  }
}

static void guardSettle() {
  if (!guardArmed || millis() < kSettleMs) return;
  guardArmed = false;
  Guard g = {kGuardMagic, 0};
  ESP.rtcUserMemoryWrite(kGuardSlot, (uint32_t*)&g, sizeof g);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nmarquee");

  Health::begin();
  LittleFS.begin();  // formats itself if the filesystem is unreadable
  settings.load();
  guardStart();

  matrix.begin();
  player.begin();
  Commands::loadRotation();

  Net::begin();
  Web::begin();
  ClaspLink::begin();
  Status::begin();

  if (!settings.ssid[0]) {
    Item msg;
    msg.kind = Item::Text;
    strlcpy(msg.arg, "SETUP - JOIN WIFI marquee-setup", sizeof msg.arg);
    msg.color = 0xFF9F43;
    msg.repeat = -1;
    player.play(msg);
  } else {
    Item msg;
    msg.kind = Item::Text;
    strlcpy(msg.arg, settings.host, sizeof msg.arg);
    msg.color = 0x33CC77;
    player.play(msg);
  }
}

void loop() {
  Health::loopStart();
  Net::loop();
  Web::loop();
  ClaspLink::loop();
  Status::loop();
  settings.tick();
  guardSettle();

  // Fixed frame clock. After a long pause (a status poll), skip ahead rather
  // than rush to catch up; animations follow the real clock anyway.
  const uint32_t now = millis();
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = (now - lastFrame > 3 * FRAME_MS) ? now : lastFrame + FRAME_MS;
    player.tick(matrix);
    matrix.show();
  }
  Health::loopEnd();
}
