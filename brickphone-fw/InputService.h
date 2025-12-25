#pragma once

#include <Arduino.h>

enum ButtonId {
  BTN_RIGHT = 0,
  BTN_UP,
  BTN_DOWN,
  BTN_LEFT,
  BTN_A,
  BTN_B,
  BTN_SELECT,
  BTN_START,
  BTN_COUNT
};

class InputService {
public:
  void begin();
  void poll(unsigned long nowMs);
  bool pressed(ButtonId id) const;
  bool released(ButtonId id) const;
  bool down(ButtonId id) const;

private:
  bool lastStable[BTN_COUNT];
  bool lastRaw[BTN_COUNT];
  bool current[BTN_COUNT];
  bool pressedEvent[BTN_COUNT];
  bool releasedEvent[BTN_COUNT];
  unsigned long lastChangeMs[BTN_COUNT];
};
