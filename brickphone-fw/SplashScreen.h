#pragma once

#include "Screen.h"
#include "AudioOutService.h"
#include "ScreenManager.h"

class SplashScreen : public Screen {
public:
  SplashScreen(AudioOutService& audio, ScreenManager& screens);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  AudioOutService& audioOut;
  ScreenManager& screenManager;
  unsigned long startMs = 0;
  bool skip = false;
  bool played = false;
};
