// A robot that lives on the panel. First its workshop: the robot's face sits
// between two turning gears, looks at one, then the other, gets to work (the
// gears speed up, sparks fly off its antenna, it chatters), then smiles as
// the gears wind down. Then it flies right to left trailing a long tail of
// stripes. About one appearance in three the stripes are the trans flag
// instead of the rainbow.
//
// Sprites are strings; the gears are drawn from a per-pixel angle table
// worked out once. Everything per frame is integer maths, so it costs less
// than the clock.
#include "robot.h"
#include <math.h>

namespace {

const int W = Matrix::W;
const int H = Matrix::H;
const int SW = 8, SH = 8;  // the face fills the panel's height

// --- colors -------------------------------------------------------------------
// No red, orange or yellow on the robot: the panel's gamma pushes orange
// toward red, and the rainbow is full of warm colors already.
const uint32_t kFaceBlue = 0x3C8CFF;
const uint32_t kEye      = 0xF0FFFF;
const uint32_t kSteel    = 0xC8CCD0;
const uint32_t kGear     = 0xA8B4C0;
const uint32_t kCyan     = 0x20E0FF;

// --- the face -----------------------------------------------------------------
// Drawn without its eyes and mouth, which change with what it is doing.
//   a antenna   f face   l bolt
const char* const kFace[SH] = {
    "...aa...", ".ffffff.", ".ffffff.", "lffffffl",
    "lffffffl", ".ffffff.", ".ffffff.", ".ffffff.",
};

enum Eyes : uint8_t { Ahead, Left, Right, Blink, Happy };
enum Mouth : uint8_t { Grille, Open, Smile };

void face(Matrix& m, int x, Eyes eyes, Mouth mouth, bool sparks, uint32_t ms) {
  for (int r = 0; r < SH; r++)
    for (int c = 0; c < SW; c++) {
      const char ch = kFace[r][c];
      uint32_t col = 0;
      if (ch == 'f') col = kFaceBlue;
      else if (ch == 'l') col = kSteel;
      else if (ch == 'a') col = sparks ? 0xFFFFFF : ((ms / 400) & 1 ? kCyan : 0x0A3848);
      if (col) m.pixel(x + c, r, col);
    }

  // Eyes: two pixels tall, at columns 2 and 5, shifted to look aside.
  const int shift = eyes == Left ? -1 : eyes == Right ? 1 : 0;
  for (int e = 0; e < 2; e++) {
    const int ex = x + (e ? 5 : 2) + shift;
    switch (eyes) {
      case Blink:
        m.pixel(ex, 4, kEye);
        break;
      case Happy:  // ^ ^
        m.pixel(ex, 3, kEye);
        m.pixel(ex - 1, 4, kEye);
        m.pixel(ex + 1, 4, kEye);
        break;
      default:
        m.pixel(ex, 3, kEye);
        m.pixel(ex, 4, kEye);
        break;
    }
  }

  // Mouth on rows 5-6.
  switch (mouth) {
    case Grille:
      m.pixel(x + 2, 6, kSteel);
      m.pixel(x + 4, 6, kSteel);
      break;
    case Open:
      for (int c = 2; c <= 5; c++) m.pixel(x + c, 6, 0);
      break;
    case Smile:
      m.pixel(x + 2, 5, kSteel);
      m.pixel(x + 5, 5, kSteel);
      for (int c = 3; c <= 4; c++) m.pixel(x + c, 6, kSteel);
      break;
  }

  // Sparks around the antenna while it works.
  if (sparks) {
    const uint32_t seed = ms / 70;
    for (uint32_t i = 0; i < 3; i++) {
      const uint32_t h = (seed + i * 7) * 2654435761u;
      const int sx = x + (int)(h >> 29);       // columns 0..7 of the face
      const int sy = (int)((h >> 20) & 1);     // rows 0..1
      if ((h >> 8) & 1) m.pixel(sx, sy, (h >> 10) & 1 ? 0xFFFFFF : kCyan);
    }
  }
}

// --- gears --------------------------------------------------------------------
// An 8x8 gear with eight teeth. For each pixel: is it the hub hole, the body,
// the tooth ring or outside, and its angle (0-255) around the center.
const int GW = 8;
uint8_t gearAngle[GW * GW];
uint8_t gearZone[GW * GW];  // 0 outside, 1 tooth ring, 2 body, 3 hole
bool gearReady = false;

void gearSetup() {
  for (int y = 0; y < GW; y++)
    for (int x = 0; x < GW; x++) {
      const int dx = 2 * x - 7, dy = 2 * y - 7;  // doubled, center at 3.5
      const int d2 = dx * dx + dy * dy;
      uint8_t zone = 0;
      if (d2 <= 2) zone = 3;
      else if (d2 <= 26) zone = 2;
      else if (d2 <= 60) zone = 1;
      gearZone[y * GW + x] = zone;
      gearAngle[y * GW + x] = (uint8_t)(int)((atan2f((float)dy, (float)dx) + PI) * 128 / PI);
    }
  gearReady = true;
}

// phase 0-255 turns the gear through one tooth pitch.
void gear(Matrix& m, int x, uint8_t phase) {
  for (int i = 0; i < GW * GW; i++) {
    const uint8_t zone = gearZone[i];
    if (!zone || zone == 3) continue;
    if (zone == 1 && (uint8_t)(gearAngle[i] * 8 + phase) >= 128) continue;
    m.pixel(x + i % GW, i / GW, zone == 2 ? kGear : kSteel);
  }
}

// --- the workshop -------------------------------------------------------------
const uint32_t kShopMs = 9000;

// How far the gears have turned by t: slowly, fast while it works, then
// slowly again. In 1/256 of a tooth pitch.
uint32_t gearTurn(uint32_t t) {
  const uint32_t slow = 90, fast = 700;  // per second
  const uint32_t workFrom = 4500, workTo = 6500;
  if (t < workFrom) return t * slow / 1000;
  const uint32_t a = workFrom * slow / 1000;
  if (t < workTo) return a + (t - workFrom) * fast / 1000;
  const uint32_t b = a + (workTo - workFrom) * fast / 1000;
  return b + (t - workTo) * slow / 1000;
}

void workshop(Matrix& m, uint32_t t, uint32_t ms) {
  if (!gearReady) gearSetup();
  const uint32_t turn = gearTurn(t);
  // The two gears turn opposite ways, as if meshed through the robot.
  gear(m, 1, (uint8_t)turn);
  gear(m, W - 1 - GW, (uint8_t)(16 - turn));

  Eyes eyes = Ahead;
  Mouth mouth = Grille;
  bool sparks = false;
  if (t < 1500) {
    eyes = (t > 1100 && t < 1260) ? Blink : Ahead;
  } else if (t < 3000) {
    eyes = Left;
  } else if (t < 4500) {
    eyes = Right;
  } else if (t < 6500) {
    sparks = true;
    mouth = ((t / 150) & 1) ? Open : Grille;  // chattering
  } else if (t < 8200) {
    eyes = Happy;
    mouth = Smile;
  } else {
    eyes = (t > 8500 && t < 8660) ? Blink : Ahead;
  }
  face(m, (W - SW) / 2, eyes, mouth, sparks, ms);
}

// --- the flight ---------------------------------------------------------------
const uint32_t kRainbow[6] = {0xFF0000, 0xFF7000, 0xFFE000, 0x00D040, 0x0060FF, 0x9000E0};
// Trans pride flag, light blue / pink / white / pink / light blue, laid over
// six rows so it stays symmetric.
const uint32_t kTrans[6] = {0x3CB4FF, 0xFF5A9A, 0xFFFFFF, 0xFFFFFF, 0xFF5A9A, 0x3CB4FF};

const int kTrail = 40;     // longer than the panel
const int kFlySpeed = 10;  // px/s
const uint32_t kFlyMs = (uint32_t)(W + SW + kTrail) * 1000 / kFlySpeed;

// In from the right, robot first, the tail following it off to the left.
void fly(Matrix& m, uint32_t t, uint32_t ms, bool trans) {
  const int x = W - (int)(t * kFlySpeed / 1000);
  const uint32_t* stripes = trans ? kTrans : kRainbow;

  // A few stars drifting the other way.
  const int span = W + 10;
  const int drift = (int)((ms / 90) % span);
  for (int i = 0; i < 5; i++) {
    const int sx = (i * 13 + 4 + drift) % span - 5;
    const int sy = (i * 5 + 1) % H;
    if (((ms / 150) + i) % 4) m.pixel(sx, sy, 0x303040);
  }

  // Tail: six stripes, stepping up and down in blocks for a bouncy look.
  const int bob = (ms / 300) & 1;
  for (int tx = x + SW; tx < x + SW + kTrail; tx++) {
    if (tx < 0 || tx >= W) continue;
    const int wave = ((tx + (int)(ms / 120)) >> 2) & 1;
    for (int s = 0; s < 6; s++) m.pixel(tx, 1 + s + wave - bob, stripes[s]);
  }

  const bool blink = (ms % 2200) < 160;
  face(m, x, blink ? Blink : Ahead, Grille, false, ms);
}

uint32_t lastT = 0;
uint32_t appearances = 0;

}  // namespace

const uint32_t Robot::kCycleMs = kShopMs + kFlyMs;

void Robot::render(Matrix& m, uint32_t elapsed, uint32_t ms) {
  // A new appearance starts at elapsed 0; count them so the flag comes up
  // now and then rather than every time.
  if (elapsed < lastT) appearances++;
  lastT = elapsed;

  m.clear();
  const uint32_t t = elapsed % kCycleMs;
  if (t < kShopMs) workshop(m, t, ms);
  else fly(m, t - kShopMs, ms, appearances % 3 == 2);
}
