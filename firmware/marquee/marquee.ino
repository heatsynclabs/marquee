// Marquee: a network-controlled LED matrix.
// https://github.com/  (see README.md)

#include <LittleFS.h>

#include "clasp_link.h"
#include "commands.h"
#include "config.h"
#include "effects.h"
#include "matrix.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "web.h"

static uint32_t lastFrame = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nmarquee");

  LittleFS.begin();
  settings.load();

  matrix.begin();
  player.begin();

  Item idle;
  idle.kind = Item::Effect;
  idle.effect = Effects::find(settings.idle);
  if (idle.effect < 0) idle.effect = 0;
  player.setIdle(idle);

  Net::begin();
  Web::begin();
  ClaspLink::begin();

  if (Net::apMode()) {
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
  Net::loop();
  Web::loop();
  ClaspLink::loop();
  settings.tick();

  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    player.tick(matrix);
    matrix.show();
  }
}
