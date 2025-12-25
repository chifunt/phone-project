#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class AppPong : public Screen {
public:
  AppPong(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  AudioOutService& audioOut;

  int16_t ballX = 64;
  int16_t ballY = 32;
  int8_t ballVX = 1;
  int8_t ballVY = 1;
  int16_t paddleY = 24;
  int16_t aiY = 24;
  int playerScore = 0;
  int aiScore = 0;
  bool paused = false;
  unsigned long lastStepMs = 0;
  unsigned long stepIntervalMs = 16;
  int8_t moveDir = 0;
};
