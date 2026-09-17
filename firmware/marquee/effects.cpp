#include "effects.h"
#include <time.h>

namespace {

const char* const kNames[] = {
    "off", "solid", "rainbow", "plasma", "fire", "rain",
    "sparkle", "comet", "chase", "breathe", "confetti", "clock",
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

void plasma(Matrix& m, uint32_t ms) {
  float t = ms / 900.0f;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v = sinf(x * 0.35f + t) + sinf(y * 0.5f - t * 1.3f) +
                sinf((x + y) * 0.22f + t * 0.7f);
      uint16_t h = (uint16_t)((v + 3.0f) * 256.0f) % 1536;
      m.pixel(x, y, hsv(h, 255, 255));
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
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  char buf[6];
  bool tick = (ms / 500) & 1;
  snprintf(buf, sizeof buf, "%02d%c%02d", tm.tm_hour, tick ? ':' : ' ', tm.tm_min);
  if (now < 100000) strcpy(buf, "--:--");
  m.text(0, 0, buf, color);
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

void render(int id, Matrix& m, uint32_t ms, uint32_t color) {
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
    default: m.clear(); break;
  }
}

}  // namespace Effects
