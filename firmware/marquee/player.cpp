#include "player.h"
#include "effects.h"
#include "font5x7.h"
#include "settings.h"
#include "status.h"

Player player;

// Header of a stored animation: "MQA1", width, height, fps.
static const size_t kHeader = 10;

void Player::begin() {
  rotation_[0] = Item();
  rotLen_ = 1;
  nextIdle();
}

void Player::setRotation(const Item* items, uint8_t count, uint32_t dwellMs) {
  if (count > kRotation) count = kRotation;
  if (!count) return;
  for (uint8_t i = 0; i < count; i++) rotation_[i] = items[i];
  rotLen_ = count;
  rotIdx_ = 0;
  if (dwellMs) dwell_ = dwellMs;
  if (idle_) nextIdle();
}

void Player::setRotation(const Item& single) { setRotation(&single, 1, 0); }

void Player::setStatusItem(const Item& it, bool present) {
  statusItem_ = it;
  statusItemSet_ = present;
}

void Player::play(const Item& it) {
  qLen_ = 0;
  qHead_ = 0;
  start(it, false);
}

bool Player::enqueue(const Item& it) {
  if (qLen_ >= kQueue) return false;
  queue_[(qHead_ + qLen_) % kQueue] = it;
  qLen_++;
  if (idle_) skip();
  return true;
}

void Player::clearQueue() { qLen_ = 0; qHead_ = 0; }

void Player::skip() {
  if (qLen_) {
    Item next = queue_[qHead_];
    qHead_ = (qHead_ + 1) % kQueue;
    qLen_--;
    start(next, false);
  } else {
    nextIdle();
  }
}

// Picks the next thing to show while nothing is queued. Every Nth turn goes to
// the status item instead, so "open" keeps coming back around.
void Player::nextIdle() {
  const uint8_t every = settings.statusEvery ? settings.statusEvery : 2;
  if (statusItemSet_ && Status::on() && (idleCount_ % every) == 0) {
    idleCount_++;
    start(statusItem_, true);
    return;
  }
  idleCount_++;
  const Item& next = rotation_[rotIdx_ % rotLen_];
  rotIdx_ = (rotIdx_ + 1) % rotLen_;
  start(next, true);
}

// A broken item must never recurse back into the rotation.
void Player::fallback(bool asIdle) {
  if (asIdle) {
    cur_ = Item();
    idle_ = true;
    start_ = millis();
  } else {
    skip();
  }
}

// Every value used as a divisor is forced into a safe range here, so a bad
// command (or a bad saved rotation) can never crash the panel.
static void sanitize(Item& it) {
  if (it.speed < 1) it.speed = 1;
  if (it.speed > 200) it.speed = 200;
  if (it.fps > 60) it.fps = 60;
  if (it.repeat < -1) it.repeat = -1;
  if (it.repeat == 0) it.repeat = 1;
  it.arg[sizeof it.arg - 1] = 0;
}

bool Player::stillFrame() const {
  if (realtimeActive()) return false;
  switch (cur_.kind) {
    case Item::Cards:
      return !cur_.rainbow;
    case Item::Text:
      return !cur_.scroll && !cur_.rainbow;
    case Item::Effect: {
      const char* n = Effects::name(cur_.effect);
      return !strcmp(n, "clock") || !strcmp(n, "solid") || !strcmp(n, "off");
    }
    default:
      return false;
  }
}

void Player::start(const Item& it, bool asIdle) {
  if (file_) file_.close();
  cur_ = it;
  sanitize(cur_);
  idle_ = asIdle;
  start_ = millis();
  lastFrame_ = -1;

  if (cur_.kind == Item::Cards) expandCards();

  if (cur_.kind == Item::Stored) {
    String path = String("/anim/") + cur_.arg;
    file_ = LittleFS.open(path, "r");
    uint8_t hdr[kHeader];
    if (!file_ || file_.read(hdr, kHeader) != (int)kHeader ||
        memcmp(hdr, "MQA1", 4) != 0) {
      if (file_) file_.close();
      fallback(asIdle);
      return;
    }
    uint16_t w = (hdr[4] << 8) | hdr[5];
    uint16_t h = (hdr[6] << 8) | hdr[7];
    uint16_t fps = (hdr[8] << 8) | hdr[9];
    if (cur_.fps) fps = cur_.fps;
    frameBytes_ = w * h * 3;
    frames_ = frameBytes_ ? (file_.size() - kHeader) / frameBytes_ : 0;
    if (!fps || fps > 60) fps = 10;
    delayMs_ = 1000 / fps;
    if (!frames_ || frameBytes_ != Matrix::N * 3) {
      file_.close();
      fallback(asIdle);
    }
  }
}

void Player::realtimeFrame(uint32_t timeoutMs) { rtUntil_ = millis() + timeoutMs; }

bool Player::realtimeActive() const {
  return rtUntil_ && (int32_t)(rtUntil_ - millis()) > 0;
}

bool Player::renderText(Matrix& m, uint32_t elapsed) {
  const int w = Matrix::textWidth(cur_.arg);
  const int y = (Matrix::H - FONT_H) / 2;
  uint16_t hue = (millis() / 8) % 1536;

  if (!cur_.scroll) {
    int x = (Matrix::W - w) / 2;
    if (cur_.rainbow) m.textRainbow(x, y, cur_.arg, hue);
    else m.text(x, y, cur_.arg, cur_.color);
    uint32_t hold = cur_.hold ? cur_.hold : 3000;
    if (cur_.repeat < 0) return false;
    return elapsed >= hold * reps();
  }

  const int travel = Matrix::W + w;
  const uint16_t speed = cur_.speed ? cur_.speed : 1;
  uint32_t span = (uint32_t)travel * 1000 / speed;
  if (!span) span = 1;
  const uint32_t loop = elapsed / span;
  const int x = Matrix::W - (int)((elapsed % span) * speed / 1000);

  if (cur_.rainbow) m.textRainbow(x, y, cur_.arg, hue);
  else m.text(x, y, cur_.arg, cur_.color);

  if (cur_.repeat < 0) return false;
  return loop >= reps();
}

bool Player::renderStored(Matrix& m, uint32_t elapsed) {
  if (!file_ || !frames_ || !delayMs_) return true;
  uint32_t tick = elapsed / delayMs_;
  uint32_t loop = tick / frames_;
  uint16_t idx = tick % frames_;

  if ((int32_t)idx != lastFrame_) {
    lastFrame_ = idx;
    file_.seek(kHeader + (uint32_t)idx * frameBytes_, SeekSet);
    file_.read(m.buf, frameBytes_);
  }
  if (cur_.repeat < 0) return false;
  return loop >= reps();
}

// Splits "OPEN|OPEN HOURS - COME IN - {until}" into cards. {until} becomes
// "UNTIL 9 PM" when the status feed gave a closing time and disappears when it
// did not, taking any separator left dangling at the end with it.
void Player::expandCards() {
  cardCount_ = 0;
  size_t w = 0;
  char until[20];
  const bool haveUntil = Status::untilText(until, sizeof until);

  const char* p = cur_.arg;
  while (*p && cardCount_ < kMaxCards) {
    const char* bar = strchr(p, '|');
    size_t n = bar ? (size_t)(bar - p) : strlen(p);

    char card[sizeof cur_.arg];
    size_t c = 0;
    for (size_t i = 0; i < n && c + 1 < sizeof card;) {
      if (n - i >= 7 && !memcmp(p + i, "{until}", 7)) {
        for (const char* u = haveUntil ? until : ""; *u && c + 1 < sizeof card; u++) card[c++] = *u;
        i += 7;
      } else {
        card[c++] = p[i++];
      }
    }
    while (c && (card[c - 1] == ' ' || card[c - 1] == '-' || card[c - 1] == ',')) c--;
    size_t s0 = 0;
    while (s0 < c && card[s0] == ' ') s0++;

    const size_t len = c - s0;
    if (len && w + len + 1 <= sizeof cardBuf_) {
      cardOff_[cardCount_++] = w;
      memcpy(cardBuf_ + w, card + s0, len);
      w += len;
      cardBuf_[w++] = 0;
    }
    if (!bar) break;
    p = bar + 1;
  }
}

// A card that fits holds still long enough to read; one wider than the panel
// scrolls through once, smoothly, at the item's speed.
uint32_t Player::cardMs(uint8_t i) const {
  const uint32_t hold = cur_.hold ? cur_.hold : 2000;
  const int w = Matrix::textWidth(cardBuf_ + cardOff_[i]);
  if (w <= Matrix::W) return hold;
  return (uint32_t)(Matrix::W + w) * 1000 / cur_.speed;
}

bool Player::renderCards(Matrix& m, uint32_t elapsed) {
  if (!cardCount_) return true;
  uint32_t total = 0;
  for (uint8_t i = 0; i < cardCount_; i++) total += cardMs(i);
  if (!total) return true;

  const uint32_t loop = elapsed / total;
  uint32_t t = elapsed % total;
  uint8_t i = 0;
  while (i < cardCount_ - 1 && t >= cardMs(i)) t -= cardMs(i++);

  const char* s = cardBuf_ + cardOff_[i];
  const int w = Matrix::textWidth(s);
  const int x = w <= Matrix::W ? (Matrix::W - w + 1) / 2
                               : Matrix::W - (int)(t * cur_.speed / 1000);
  const uint16_t hue = (millis() / 8) % 1536;
  if (cur_.rainbow) m.textRainbow(x, 0, s, hue);
  else m.text(x, 0, s, cur_.color);

  if (cur_.repeat < 0) return false;
  return loop >= reps();
}

void Player::tick(Matrix& m) {
  if (realtimeActive()) return;  // panel is driven from outside

  const uint32_t elapsed = millis() - start_;
  bool done = false;

  switch (cur_.kind) {
    case Item::Text:
      m.clear();
      done = renderText(m, elapsed);
      break;
    case Item::Stored:
      done = renderStored(m, elapsed);
      break;
    case Item::Cards:
      m.clear();
      done = renderCards(m, elapsed);
      break;
    default:
      Effects::render(cur_.effect, m, millis(), elapsed, cur_.color);
      done = cur_.hold && elapsed >= cur_.hold;
      break;
  }

  // Cards run their own sequence to the end; hold is per card there.
  if (!done && idle_ && cur_.kind != Item::Cards &&
      rotLen_ + (statusItemSet_ ? 1 : 0) > 1) {
    // Only move along on time when there is somewhere else to go.
    uint32_t limit = cur_.hold ? cur_.hold : dwell_;
    done = elapsed >= limit;
  }

  if (done) {
    if (idle_) nextIdle();
    else skip();
  }
}
