#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class AppSpaceInvaders : public Screen {
public:
  AppSpaceInvaders(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  static const int kCols = 8;
  static const int kRows = 4;
  static const int kAlienW = 8;
  static const int kAlienH = 8;

  void reset();

  AudioOutService& audioOut;
  bool aliens[kRows][kCols];
  int16_t swarmX = 8;
  int16_t swarmY = 10;
  int8_t swarmDir = 1;
  unsigned long lastStepMs = 0;
  unsigned long stepIntervalMs = 220;

  int16_t playerX = 56;
  bool bulletActive = false;
  int16_t bulletX = 0;
  int16_t bulletY = 0;
  int8_t moveDir = 0;
  unsigned long lastPlayerStepMs = 0;
  unsigned long playerStepIntervalMs = 30;
  bool animFrame = false;
  bool won = false;
  bool lost = false;
};
