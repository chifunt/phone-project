#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class AppFlappy : public Screen {
public:
  AppFlappy(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  void reset();

  AudioOutService& audioOut;
  float birdY = 32.0f;
  float birdV = 0.0f;
  int pipeX[2];
  int pipeGapY[2];
  int score = 0;
  bool dead = false;
  unsigned long lastStepMs = 0;
  unsigned long stepIntervalMs = 16;
};
