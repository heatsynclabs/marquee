// Pixel buffer, coordinate mapping and text drawing.
//
// Drawing happens in a plain RGB buffer laid out row-major with (0,0) at the
// top-left. show() maps it onto the physical strip, applies gamma, brightness
// and the power budget, and pushes it out.
#pragma once
#include <Arduino.h>
#include "config.h"

class Matrix {
 public:
  static const int W = MATRIX_WIDTH;
  static const int H = MATRIX_HEIGHT;
  static const int N = W * H;

  void begin();
  void clear() { memset(buf, 0, sizeof buf); }
  void fill(uint32_t c);
  void pixel(int x, int y, uint32_t c);
  uint32_t get(int x, int y) const;

  // Draws with the top of the glyph at y. Returns the x advance.
  int text(int x, int y, const char* s, uint32_t color);
  // Same, but each column takes its hue from x, giving a rainbow fill.
  int textRainbow(int x, int y, const char* s, uint16_t hue);
  static int textWidth(const char* s);

  // Copies raw RGB bytes into the buffer starting at a pixel offset.
  void blit(const uint8_t* rgb, size_t bytes, size_t firstPixel = 0);

  // Pushes the frame to the LEDs if it changed. Returns true when it did.
  bool show(bool force = false);
  uint16_t lastDrawMa() const { return lastMa_; }
  uint32_t pushes() const { return pushes_; }

  static uint16_t index(int x, int y);

  uint8_t buf[N * 3];

 private:
  uint16_t lastMa_ = 0;
  uint32_t lastHash_ = 0, lastPush_ = 0, pushes_ = 0;
};

uint32_t hsv(uint16_t h, uint8_t s, uint8_t v);  // h: 0-1535 (6*256)

extern Matrix matrix;
