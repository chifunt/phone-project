#pragma once

#include <Arduino.h>

class DisplayService;
class InputService;

class Screen {
public:
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void tick(unsigned long dtMs) { (void)dtMs; }
  virtual void handleInput(InputService&) {}
  virtual void render(DisplayService&) = 0;
};
