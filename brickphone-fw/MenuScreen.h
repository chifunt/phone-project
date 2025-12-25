#pragma once

#include "Screen.h"
#include "ScreenManager.h"
#include "AudioOutService.h"

class MenuScreen : public Screen {
public:
  struct Entry {
    const char* label;
    const uint8_t* icon;
    ScreenId id;
  };

  MenuScreen(ScreenManager& screens, AudioOutService& audio);
  void handleInput(InputService& input) override;
  void render(DisplayService& display) override;

private:
  ScreenManager& screenManager;
  AudioOutService& audioOut;
  uint8_t selected = 0;
};
