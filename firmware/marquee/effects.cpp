#include "effects.h"
#include "robot.h"
#include <sys/time.h>
#include <time.h>

namespace {

const char* const kNames[] = {
    "off", "solid", "rainbow", "plasma", "fire", "rain",
    "sparkle", "comet", "chase", "breathe", "confetti", "clock", "robot",
};
const int kCount = sizeof(kNames) / sizeof(kNames[0]);

const int W = Matrix::W;
const int H = Matrix::H;

uint8_t rnd8() { return (uint8_t)RANDOM_REG32; }

uint32_t scaleColor(uint32_t c, uint8_t v) {
  uint8_t r = ((c >> 16 & 0xFF) * v) >> 8;
  uint8_t g = ((c >> 8 & 0xFF) * v) >> 8;
  uint8_t b = ((c & 0xFF) * v) >> 8;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void fadeAll(Matrix& m, uint8_t keep) {
  for (size_t i = 0; i < sizeof m.buf; i++) m.buf[i] = (m.buf[i] * keep) >> 8;
}

void rainbow(Matrix& m, uint32_t ms) {
  uint16_t base = (ms / 6) % 1536;
  for (int x = 0; x < W; x++) {
    uint32_t c = hsv(base + x * 40, 255, 255);
    for (int y = 0; y < H; y++) m.pixel(x, y, c);
  }
}

// The ESP8266 has no FPU: 720 sinf() calls a frame starved Wi-Fi. A 256-entry
// sine table gives the same picture with integer maths.
uint8_t sine[256];
bool sineReady = false;

inline uint8_t sin8(uint8_t a) { return sine[a]; }

void plasma(Matrix& m, uint32_t ms) {
  if (!sineReady) {
    for (int i = 0; i < 256; i++) sine[i] = (uint8_t)(127.5f + 127.5f * sinf(i * 2 * PI / 256));
    sineReady = true;
  }
  // Angles in 1/256 turn. Same speeds and spatial frequencies as before.
  const uint8_t t1 = ms / 22, t2 = (uint64_t)ms * 13 / 220, t3 = (uint64_t)ms * 7 / 220;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint16_t v = sin8(x * 14 + t1) + sin8(y * 20 - t2) + sin8((x + y) * 9 + t3);
      m.pixel(x, y, hsv((v * 2) % 1536, 255, 255));
    }
}

void fire(Matrix& m, uint32_t ms) {
  static uint8_t heat[W * H];
  static uint32_t last = 0;
  if (ms - last >= 40) {
    last = ms;
    for (int x = 0; x < W; x++) {
      for (int y = 0; y < H - 1; y++) {
        int below = heat[(y + 1) * W + x];
        int cool = rnd8() % 24;
        heat[y * W + x] = below > cool ? below - cool : 0;
      }
      uint8_t& seed = heat[(H - 1) * W + x];
      seed = (rnd8() % 5 == 0) ? 200 + rnd8() % 55 : (seed > 12 ? seed - 12 : 0);
    }
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t h = heat[y * W + x];
      m.pixel(x, y, hsv(h / 6, 255, h));  // red through yellow
    }
}

void rain(Matrix& m, uint32_t ms) {
  static int8_t drop[W];
  static uint32_t last = 0;
  fadeAll(m, 160);
  if (ms - last >= 60) {
    last = ms;
    for (int x = 0; x < W; x++) {
      if (drop[x] > 0) {
        drop[x]++;
        if (drop[x] > H + 2) drop[x] = 0;
      } else if (rnd8() < 10) {
        drop[x] = 1;
      }
    }
  }
  for (int x = 0; x < W; x++) {
    int y = drop[x] - 1;
    if (y >= 0 && y < H) m.pixel(x, y, 0x60FF80);
  }
}

void sparkle(Matrix& m, uint32_t ms, uint32_t color) {
  static uint32_t last = 0;
  fadeAll(m, 200);
  if (ms - last >= 40) {
    last = ms;
    for (int i = 0; i < 3; i++) m.pixel(rnd8() % W, rnd8() % H, color);
  }
}

void comet(Matrix& m, uint32_t ms, uint32_t color) {
  fadeAll(m, 205);
  int span = W + 8;
  int x = (ms / 30) % span - 4;
  for (int t = 0; t < 4; t++) {
    uint8_t v = 255 >> t;
    for (int y = 0; y < H; y++) m.pixel(x - t, y, scaleColor(color, v));
  }
}

void chase(Matrix& m, uint32_t ms, uint32_t color) {
  int step = (ms / 120) % 3;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++)
      m.pixel(x, y, ((x + y + step) % 3 == 0) ? color : 0);
}

void breathe(Matrix& m, uint32_t ms, uint32_t color) {
  float phase = (ms % 4000) / 4000.0f * 2 * PI;
  uint8_t v = 12 + (uint8_t)((sinf(phase) * 0.5f + 0.5f) * 243);
  m.fill(scaleColor(color, v));
}

void confetti(Matrix& m, uint32_t ms) {
  static uint32_t last = 0;
  fadeAll(m, 230);
  if (ms - last >= 50) {
    last = ms;
    for (int i = 0; i < 2; i++)
      m.pixel(rnd8() % W, rnd8() % H, hsv((uint16_t)rnd8() * 6, 200, 255));
  }
}

void clock(Matrix& m, uint32_t ms, uint32_t color) {
  (void)ms;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  char buf[6];
  if (tv.tv_sec < 1700000000) {  // NTP has not answered yet
    strcpy(buf, "--:--");
  } else {
    struct tm tm;
    time_t now = tv.tv_sec;
    localtime_r(&now, &tm);
    // Colon on for the first half of each real second.
    const bool colon = tv.tv_usec < 500000;
    snprintf(buf, sizeof buf, "%02d%c%02d", tm.tm_hour, colon ? ':' : ' ', tm.tm_min);
  }
  m.text(0, 0, buf, color, true);  // fixed width so the digits stay put
}

}  // namespace

namespace Effects {

int count() { return kCount; }

const char* name(int id) { return (id >= 0 && id < kCount) ? kNames[id] : ""; }

int find(const char* n) {
  if (!n || !*n) return -1;
  for (int i = 0; i < kCount; i++)
    if (strcasecmp(n, kNames[i]) == 0) return i;
  return -1;
}

void render(int id, Matrix& m, uint32_t ms, uint32_t elapsed, uint32_t color) {
  switch (id) {
    case 0:  m.clear(); break;
    case 1:  m.fill(color); break;
    case 2:  rainbow(m, ms); break;
    case 3:  plasma(m, ms); break;
    case 4:  fire(m, ms); break;
    case 5:  rain(m, ms); break;
    case 6:  sparkle(m, ms, color); break;
    case 7:  comet(m, ms, color); break;
    case 8:  chase(m, ms, color); break;
    case 9:  breathe(m, ms, color); break;
    case 10: confetti(m, ms); break;
    case 11: m.clear(); clock(m, ms, color); break;
    case 12: Robot::render(m, elapsed, ms); break;
    default: m.clear(); break;
  }
}

}  // namespace Effects
