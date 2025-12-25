#include "InputService.h"
#include "Pins.h"

static const uint8_t kPins[BTN_COUNT] = {
  PIN_BTN_RIGHT,
  PIN_BTN_UP,
  PIN_BTN_DOWN,
  PIN_BTN_LEFT,
  PIN_BTN_A,
  PIN_BTN_B,
  PIN_BTN_SELECT,
  PIN_BTN_START
};

static const unsigned long kDebounceMs = 25;

void InputService::begin() {
  for (int i = 0; i < BTN_COUNT; ++i) {
    pinMode(kPins[i], INPUT_PULLUP);
    bool raw = digitalRead(kPins[i]) == LOW;
    lastStable[i] = raw;
    lastRaw[i] = raw;
    current[i] = raw;
    pressedEvent[i] = false;
    releasedEvent[i] = false;
    lastChangeMs[i] = 0;
  }
}

void InputService::poll(unsigned long nowMs) {
  for (int i = 0; i < BTN_COUNT; ++i) {
    pressedEvent[i] = false;
    releasedEvent[i] = false;

    bool rawPressed = digitalRead(kPins[i]) == LOW;
    if (rawPressed != lastRaw[i]) {
      lastRaw[i] = rawPressed;
      lastChangeMs[i] = nowMs;
    }

    if ((nowMs - lastChangeMs[i]) >= kDebounceMs && rawPressed != lastStable[i]) {
      lastStable[i] = rawPressed;
      if (rawPressed) pressedEvent[i] = true;
      else releasedEvent[i] = true;
    }
    current[i] = lastStable[i];
  }
}

bool InputService::pressed(ButtonId id) const {
  return pressedEvent[id];
}

bool InputService::released(ButtonId id) const {
  return releasedEvent[id];
}

bool InputService::down(ButtonId id) const {
  return current[id];
}
