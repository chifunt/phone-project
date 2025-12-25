#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class AppSnake : public Screen {
public:
  AppSnake(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  enum Dir { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
  struct Pt { uint8_t x; uint8_t y; };

  void resetGame();
  void spawnFood();
  bool contains(uint8_t x, uint8_t y);
  bool isOpposite(Dir a, Dir b);

  AudioOutService& audioOut;
  static const int CELL = 4;
  static const int GRID_W = 128 / CELL;
  static const int GRID_H = 64 / CELL;
  static const int MAX_CELLS = GRID_W * GRID_H;

  Pt snake[MAX_CELLS];
  int snakeLen = 0;
  Pt food;
  int score = 0;
  Dir dir = DIR_RIGHT;
  Dir nextDir = DIR_RIGHT;
  bool running = false;
  bool gameOver = false;
  unsigned long lastStepMs = 0;
  unsigned long stepIntervalMs = 120;
  bool fastMode = false;
  bool soundEnabled = true;
};
