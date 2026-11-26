// A small robot that lives on the panel: it walks in, waves, hops and walks
// off. Between its walks the HeatSync Labs mark flies across trailing
// stripes; about one appearance in three they are the trans flag instead of
// the rainbow.
//
// Everything is a 6x7 sprite drawn from strings plus integer maths, so it
// costs less than the clock.
#include "robot.h"

namespace {

const int W = Matrix::W;
const int H = Matrix::H;
const int SW = 6, SH = 7;

// Sprite rows, top to bottom. Letters pick colors from ink().
//   a antenna tip   h head   e visor   b body   r arm   l leg   f flame
const char* const kWalkA[SH] = {
    "..a...", ".hhhh.", ".heeh.", ".hhhh.", "rbbbbr", ".bbbb.", ".l..l.",
};
const char* const kWalkB[SH] = {
    "..a...", ".hhhh.", ".heeh.", ".hhhh.", "rbbbbr", ".bbbb.", "..ll..",
};
const char* const kWaveUp[SH] = {
    "..a..r", ".hhhhr", ".heeh.", ".hhhh.", "rbbbb.", ".bbbb.", ".l..l.",
};
const char* const kWaveDown[SH] = {
    "..a...", ".hhhh.", ".heehr", ".hhhhr", "rbbbb.", ".bbbb.", ".l..l.",
};
// The HeatSync Labs mark at 8x8: the orange sun with its curved arrow cut
// out. The mark's flames are black, which an LED cannot show, so a couple of
// embers flicker where they would be instead.
const int LW = 9, LH = 8;
const char* const kLogo[LH] = {
    "..oooo.x.",
    ".o.oooo.x",
    "o.oooooo.",
    "oo.ooooox",
    "o...oooo.",
    "oo.oooox.",
    ".oo...o.x",
    "..oooo...",
};

uint32_t ink(char c, uint32_t ms) {
  switch (c) {
    case 'a': return (ms / 400) & 1 ? 0xFF2020 : 0x400000;  // blinking tip
    case 'h': return 0xB8C4CC;
    case 'e': return 0x20E0FF;
    case 'b': return 0xFF8A1E;                               // HeatSync orange
    case 'r': return 0xB8C4CC;
    case 'l': return 0x8090A0;
    case 'o': return 0xF99A1C;                               // the mark's orange
    case 'x': return (ms / 140) & 1 ? 0xC02000 : 0x501000;   // embers
    default:  return 0;
  }
}

void sprite(Matrix& m, const char* const* rows, int x, int y, uint32_t ms,
            int w = SW, int h = SH) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      char ch = rows[r][c];
      if (ch != '.') m.pixel(x + c, y + r, ink(ch, ms));
    }
}

const uint32_t kRainbow[6] = {0xFF0000, 0xFF7000, 0xFFE000, 0x00D040, 0x0060FF, 0x9000E0};
// Trans pride flag, light blue / pink / white / pink / light blue, laid over
// six rows so it stays symmetric.
const uint32_t kTrans[6] = {0x3CB4FF, 0xFF5A9A, 0xFFFFFF, 0xFFFFFF, 0xFF5A9A, 0x3CB4FF};

// Walk: in from the left, stop in the middle to wave and hop, walk off.
const uint32_t kWalkMs = 9000;
void walk(Matrix& m, uint32_t t, uint32_t ms) {
  const int mid = (W - SW) / 2;
  const uint32_t inMs = (uint32_t)(mid + SW) * 1000 / 8;  // 8 px/s
  int x, y = 1;
  const char* const* pose;

  if (t < inMs) {
    x = -SW + (int)(t * 8 / 1000);
    pose = ((t / 250) & 1) ? kWalkA : kWalkB;
  } else if (t < inMs + 1600) {
    x = mid;
    pose = (((t - inMs) / 200) & 1) ? kWaveUp : kWaveDown;
  } else if (t < inMs + 2400) {
    x = mid;
    const uint32_t h = t - inMs - 1600;  // two little hops
    y = ((h / 200) & 1) ? 0 : 1;
    pose = kWalkB;
  } else {
    const uint32_t o = t - inMs - 2400;
    x = mid + (int)(o * 8 / 1000);
    pose = ((o / 250) & 1) ? kWalkA : kWalkB;
  }
  if (x < W) sprite(m, pose, x, y, ms);
}

// Fly: the mark crosses with a stripe trail behind it that follows it off.
const int kTrail = 26;
const uint32_t kFlyMs = (uint32_t)(W + LW + kTrail) * 1000 / 12;  // 12 px/s
void fly(Matrix& m, uint32_t t, uint32_t ms, bool trans) {
  const int x = -LW + (int)(t * 12 / 1000);
  const int bob = ((ms / 300) & 1);
  const uint32_t* stripes = trans ? kTrans : kRainbow;

  // A few stars drifting the other way.
  const int span = W + 10;
  const int drift = (int)((ms / 90) % span);
  for (int i = 0; i < 5; i++) {
    int sx = ((i * 13 + 40 - i * 7 - drift) % span + span) % span;
    int sy = (i * 5 + 1) % H;
    if (((ms / 150) + i) % 4) m.pixel(sx, sy, 0x303040);
  }

  // Trail: six stripes, stepping up and down in blocks for a bouncy look.
  for (int tx = x - kTrail; tx < x; tx++) {
    if (tx < 0 || tx >= W) continue;
    const int wave = (((tx - (int)(ms / 120)) >> 2) & 1);
    for (int s = 0; s < 6; s++) m.pixel(tx, 1 + s + wave - bob, stripes[s]);
  }
  sprite(m, kLogo, x, 0, ms, LW, LH);
}

uint32_t lastT = 0;
uint32_t appearances = 0;

}  // namespace

const uint32_t Robot::kCycleMs = 2 * kWalkMs + kFlyMs;

void Robot::render(Matrix& m, uint32_t elapsed, uint32_t ms) {
  // A new appearance starts at elapsed 0; count them so the flag comes up
  // now and then rather than every time.
  if (elapsed < lastT) appearances++;
  lastT = elapsed;

  m.clear();
  uint32_t t = elapsed % kCycleMs;
  if (t < kWalkMs) {
    walk(m, t, ms);
  } else if (t < kWalkMs + kFlyMs) {
    fly(m, t - kWalkMs, ms, appearances % 3 == 2);
  } else {
    walk(m, t - kWalkMs - kFlyMs, ms);
  }
}
