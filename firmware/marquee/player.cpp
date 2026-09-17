#include "player.h"
#include "effects.h"
#include "font5x7.h"

Player player;

// Header of a stored animation: "MQA1", width, height, fps.
static const size_t kHeader = 10;

void Player::begin() {
  idleItem_ = Item();
  start(idleItem_, true);
}

void Player::setIdle(const Item& it) {
  idleItem_ = it;
  if (idle_) start(idleItem_, true);
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
    start(idleItem_, true);
  }
}

// A broken item must never recurse back into the idle item.
void Player::fallback(bool asIdle) {
  if (asIdle) {
    cur_ = Item();
    idle_ = true;
    start_ = millis();
  } else {
    skip();
  }
}

void Player::start(const Item& it, bool asIdle) {
  if (file_) file_.close();
  cur_ = it;
  idle_ = asIdle;
  start_ = millis();
  lastFrame_ = -1;

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
    delayMs_ = fps ? 1000 / fps : 100;
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
  const uint32_t span = (uint32_t)travel * 1000 / speed;
  const uint32_t loop = span ? elapsed / span : 0;
  const int x = Matrix::W - (int)((elapsed % span) * speed / 1000);

  if (cur_.rainbow) m.textRainbow(x, y, cur_.arg, hue);
  else m.text(x, y, cur_.arg, cur_.color);

  if (cur_.repeat < 0) return false;
  return loop >= reps();
}

bool Player::renderStored(Matrix& m, uint32_t elapsed) {
  if (!file_ || !frames_) return true;
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
    default:
      Effects::render(cur_.effect, m, millis(), cur_.color);
      done = cur_.hold && elapsed >= cur_.hold;
      break;
  }

  if (done && !idle_) skip();
}
