// What the panel is showing right now, plus the queue of what comes next.
//
// Every request turns into an Item. Items are played in order. When the queue
// drains the panel falls back to the idle rotation: a short list of items that
// take turns, with an extra entry mixed in while the status poll says its
// watched value is true.
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "matrix.h"

struct Item {
  enum Kind : uint8_t { Effect, Text, Stored };

  uint8_t  kind    = Effect;
  char     arg[128] = "";     // message text, or a stored animation name
  int8_t   effect  = 0;       // Effects id, for kind == Effect
  uint32_t color   = 0xFFFFFF;
  bool     rainbow = false;
  bool     scroll  = true;
  uint16_t speed   = 20;      // pixels per second
  int16_t  repeat  = 1;       // -1 repeats forever
  uint32_t hold    = 0;       // ms; 0 means "derive from repeat"
  uint16_t fps     = 0;       // stored animation override
};

class Player {
 public:
  static const uint8_t kRotation = 6;
  static const uint8_t kQueue = 8;

  void begin();

  void play(const Item& it);      // drops the queue and starts now
  bool enqueue(const Item& it);   // false when the queue is full
  void clearQueue();
  void skip();

  // Idle rotation.
  void setRotation(const Item* items, uint8_t count, uint32_t dwellMs);
  void setRotation(const Item& single);        // convenience: one entry
  void setStatusItem(const Item& it, bool present);
  bool hasStatusItem() const { return statusItemSet_; }
  const Item& statusItem() const { return statusItem_; }
  uint8_t rotationCount() const { return rotLen_; }
  const Item& rotationAt(uint8_t i) const { return rotation_[i]; }
  uint32_t dwellMs() const { return dwell_; }
  void setDwell(uint32_t ms) { dwell_ = ms ? ms : 1000; }

  // A realtime source (UDP or a WebSocket binary frame) took over the panel.
  void realtimeFrame(uint32_t timeoutMs);
  bool realtimeActive() const;

  void tick(Matrix& m);

  const Item& current() const { return cur_; }
  bool  idleNow() const { return idle_; }
  uint8_t queued() const { return qLen_; }

 private:
  void start(const Item& it, bool asIdle);
  void fallback(bool asIdle);
  void nextIdle();
  bool renderText(Matrix& m, uint32_t elapsed);
  bool renderStored(Matrix& m, uint32_t elapsed);
  uint32_t reps() const { return cur_.repeat > 0 ? (uint32_t)cur_.repeat : 1; }

  Item cur_;
  bool idle_ = true;

  Item     rotation_[kRotation];
  uint8_t  rotLen_ = 1, rotIdx_ = 0;
  uint32_t dwell_ = 15000;
  uint32_t idleCount_ = 0;

  Item statusItem_;
  bool statusItemSet_ = false;

  Item     queue_[kQueue];
  uint8_t  qHead_ = 0, qLen_ = 0;

  uint32_t start_ = 0;
  uint32_t rtUntil_ = 0;

  File     file_;
  uint16_t frames_ = 0, frameBytes_ = 0, delayMs_ = 100;
  int32_t  lastFrame_ = -1;
};

extern Player player;
