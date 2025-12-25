#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class AppBreakout : public Screen {
public:
  AppBreakout(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  static const int kCols = 8;
  static const int kRows = 4;
  static const int kBrickW = 14;
  static const int kBrickH = 6;

  void reset();

  AudioOutService& audioOut;
  bool bricks[kRows][kCols];
  int16_t ballX = 64;
  int16_t ballY = 40;
  int8_t ballVX = 1;
  int8_t ballVY = -1;
  int16_t paddleX = 50;
  bool launched = false;
  unsigned long lastStepMs = 0;
  unsigned long stepIntervalMs = 20;
  int8_t moveDir = 0;
  bool won = false;
};
