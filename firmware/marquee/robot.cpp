// A robot that lives on the panel: it walks in, waves, hops and walks off,
// then flies across on a jetpack trailing stripes. About one appearance in
// three the stripes are the trans flag instead of the rainbow.
//
// Everything is an 8x8 sprite drawn from strings plus integer maths, so it
// costs less than the clock.
#include "robot.h"

namespace {

const int W = Matrix::W;
const int H = Matrix::H;
const int SW = 8, SH = 8;  // the full height of the panel

// Sprite rows, top to bottom. Letters pick colors from ink().
//   a antenna tip   h head   e eye   b body   r arm   l leg   f flame
const char* const kWalkA[SH] = {
    "....a...", ".hhhhhh.", ".hehheh.", ".hhhhhh.",
    "rbbbbbbr", "rbbbbbbr", ".bbbbbb.", ".ll..ll.",
};
const char* const kWalkB[SH] = {
    "....a...", ".hhhhhh.", ".hehheh.", ".hhhhhh.",
    "rbbbbbbr", "rbbbbbbr", ".bbbbbb.", "...ll...",
};
const char* const kWaveUp[SH] = {
    "....a..r", ".hhhhhhr", ".hehhehr", ".hhhhhhr",
    "rbbbbbb.", "rbbbbbb.", ".bbbbbb.", ".ll..ll.",
};
const char* const kWaveDown[SH] = {
    "....a...", ".hhhhhh.", ".hehheh.", ".hhhhhhr",
    "rbbbbbbr", "rbbbbbb.", ".bbbbbb.", ".ll..ll.",
};
const char* const kFly[SH] = {
    "....a...", ".hhhhhh.", ".hehheh.", ".hhhhhhr",
    "fbbbbbbr", "fbbbbbb.", "fbbbbbb.", "ll......",
};

// No red anywhere on the robot.
uint32_t ink(char c, uint32_t ms) {
  switch (c) {
    case 'a': return (ms / 400) & 1 ? 0x20E0FF : 0x0A3848;  // blinking tip
    case 'h': return 0xB8C4CC;
    case 'e': return 0x20E0FF;
    case 'b': return 0xFF9F43;                               // HeatSync orange
    case 'r': return 0xB8C4CC;
    case 'l': return 0x8090A0;
    case 'f': return (ms / 80) & 1 ? 0xFFE040 : 0xFFFFC0;    // flicker
    default:  return 0;
  }
}

void sprite(Matrix& m, const char* const* rows, int x, int y, uint32_t ms) {
  for (int r = 0; r < SH; r++)
    for (int c = 0; c < SW; c++) {
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
  int x, y = 0;
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
    y = ((h / 200) & 1) ? -1 : 0;
    pose = kWalkB;
  } else {
    const uint32_t o = t - inMs - 2400;
    x = mid + (int)(o * 8 / 1000);
    pose = ((o / 250) & 1) ? kWalkA : kWalkB;
  }
  if (x < W) sprite(m, pose, x, y, ms);
}

// Fly: the robot crosses with a stripe trail behind it that follows it off.
const int kTrail = 26;
const uint32_t kFlyMs = (uint32_t)(W + SW + kTrail) * 1000 / 12;  // 12 px/s
void fly(Matrix& m, uint32_t t, uint32_t ms, bool trans) {
  const int x = -SW + (int)(t * 12 / 1000);
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
  sprite(m, kFly, x, 0, ms);
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
