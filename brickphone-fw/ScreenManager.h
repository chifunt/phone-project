#pragma once

#include "Screen.h"
#include "InputService.h"

enum class ScreenId {
  Splash = 0,
  Menu,
  Snake,
  Recorder,
  Voice,
  Settings
};

class ScreenManager {
public:
  void registerScreen(ScreenId id, Screen* screen);
  void set(ScreenId id);
  void tick(unsigned long dtMs, InputService& input);
  void render(DisplayService& display);
  ScreenId currentId() const { return current; }

private:
  Screen* screens[6] = {};
  ScreenId current = ScreenId::Splash;
  Screen* currentScreen = nullptr;
};
