#include "matrix.h"
#include "font5x7.h"
#include "settings.h"
#include <Adafruit_NeoPixel.h>

Matrix matrix;
static Adafruit_NeoPixel strip(Matrix::N, LED_PIN, NEO_GRB + NEO_KHZ800);

// Rough WS2812B draw: one channel at full is about 20 mA, plus ~1 mA idle.
static const uint16_t kIdleMaPerPixel = 1;

void Matrix::begin() {
  strip.begin();
  strip.setBrightness(255);  // brightness is applied in show()
  strip.clear();
  strip.show();
  clear();
}

uint16_t Matrix::index(int x, int y) {
  if (!MATRIX_ROW0_TOP) y = H - 1 - y;
  bool flip = MATRIX_ROW0_RIGHT;
  if (MATRIX_SERPENTINE && (y & 1)) flip = !flip;
  if (flip) x = W - 1 - x;
  return y * W + x;
}

void Matrix::pixel(int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  uint8_t* p = &buf[(y * W + x) * 3];
  p[0] = c >> 16;
  p[1] = c >> 8;
  p[2] = c;
}

uint32_t Matrix::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= W || y >= H) return 0;
  const uint8_t* p = &buf[(y * W + x) * 3];
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

void Matrix::fill(uint32_t c) {
  for (int i = 0; i < N; i++) {
    uint8_t* p = &buf[i * 3];
    p[0] = c >> 16;
    p[1] = c >> 8;
    p[2] = c;
  }
}

void Matrix::blit(const uint8_t* rgb, size_t bytes, size_t firstPixel) {
  size_t off = firstPixel * 3;
  if (off >= sizeof buf) return;
  size_t n = min(bytes, sizeof buf - off);
  memcpy(buf + off, rgb, n);
}

static uint8_t glyphColumn(char ch, int col) {
  if (ch < 32 || ch > 126) ch = '?';
  return pgm_read_byte(&FONT5X7[(ch - 32) * FONT_W + col]);
}

int Matrix::textWidth(const char* s) {
  int n = strlen(s);
  return n ? n * (FONT_W + 1) - 1 : 0;
}

int Matrix::text(int x, int y, const char* s, uint32_t color) {
  int x0 = x;
  for (const char* p = s; *p; p++) {
    for (int c = 0; c < FONT_W; c++, x++) {
      if (x < 0 || x >= W) continue;
      uint8_t bits = glyphColumn(*p, c);
      for (int r = 0; r < FONT_H; r++)
        if (bits & (1 << r)) pixel(x, y + r, color);
    }
    x++;  // one column of spacing
  }
  return x - x0;
}

int Matrix::textRainbow(int x, int y, const char* s, uint16_t hue) {
  int x0 = x;
  for (const char* p = s; *p; p++) {
    for (int c = 0; c < FONT_W; c++, x++) {
      if (x < 0 || x >= W) continue;
      uint32_t col = hsv(hue + x * 40, 255, 255);
      uint8_t bits = glyphColumn(*p, c);
      for (int r = 0; r < FONT_H; r++)
        if (bits & (1 << r)) pixel(x, y + r, col);
    }
    x++;
  }
  return x - x0;
}

// FNV-1a over the frame and the settings that change what reaches the LEDs.
static uint32_t frameHash(const uint8_t* b, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  h = (h ^ settings.brightness) * 16777619u;
  h = (h ^ (settings.powerMa & 0xFF)) * 16777619u;
  return (h ^ (settings.powerMa >> 8)) * 16777619u;
}

bool Matrix::show(bool force) {
  // Pushing 240 pixels turns interrupts off for about 7 ms, which Wi-Fi feels.
  // Skip identical frames, but re-push now and then in case a glitch on the
  // data line left a pixel wrong.
  const uint32_t h = frameHash(buf, sizeof buf);
  const uint32_t now = millis();
  if (!force && h == lastHash_ && now - lastPush_ < REFRESH_MS) return false;
  lastHash_ = h;
  lastPush_ = now;
  pushes_++;

  uint8_t* out = strip.getPixels();
  uint32_t sum = 0;
  const uint16_t br = (uint16_t)settings.brightness + 1;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const uint8_t* s = &buf[(y * W + x) * 3];
      uint8_t r = (Adafruit_NeoPixel::gamma8(s[0]) * br) >> 8;
      uint8_t g = (Adafruit_NeoPixel::gamma8(s[1]) * br) >> 8;
      uint8_t b = (Adafruit_NeoPixel::gamma8(s[2]) * br) >> 8;
      sum += (uint32_t)r + g + b;
      strip.setPixelColor(index(x, y), r, g, b);
    }
  }

  const uint32_t idleMa = (uint32_t)N * kIdleMaPerPixel;
  uint32_t ledMa = (sum * 20) / 255;
  uint32_t budget = settings.powerMa;

  if (budget && ledMa + idleMa > budget) {
    uint32_t allowed = budget > idleMa ? budget - idleMa : 0;
    uint32_t scale = ledMa ? (allowed * 256) / ledMa : 0;
    if (scale > 256) scale = 256;
    for (int i = 0; i < N * 3; i++) out[i] = (out[i] * scale) >> 8;
    ledMa = allowed;
  }
  lastMa_ = ledMa + idleMa;

  strip.show();
  return true;
}

// Six-segment hue ramp. h is 0-1535, wrapping.
uint32_t hsv(uint16_t h, uint8_t s, uint8_t v) {
  h %= 1536;
  uint8_t seg = h >> 8, off = h & 0xFF;
  uint8_t r, g, b;
  switch (seg) {
    case 0:  r = 255;       g = off;       b = 0; break;
    case 1:  r = 255 - off; g = 255;       b = 0; break;
    case 2:  r = 0;         g = 255;       b = off; break;
    case 3:  r = 0;         g = 255 - off; b = 255; break;
    case 4:  r = off;       g = 0;         b = 255; break;
    default: r = 255;       g = 0;         b = 255 - off; break;
  }
  // desaturate then scale by value
  r = (uint8_t)(255 - (uint16_t)(255 - r) * s / 255);
  g = (uint8_t)(255 - (uint16_t)(255 - g) * s / 255);
  b = (uint8_t)(255 - (uint16_t)(255 - b) * s / 255);
  return ((uint32_t)(r * v / 255) << 16) | ((uint32_t)(g * v / 255) << 8) | (b * v / 255);
}
