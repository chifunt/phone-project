#pragma once

#include "Screen.h"
#include "AudioOutService.h"

class App2048 : public Screen {
public:
  App2048(AudioOutService& audio);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  void reset();
  bool move(int dx, int dy);
  bool spawnTile();
  bool hasMoves() const;

  AudioOutService& audioOut;
  uint16_t grid[4][4];
  bool won = false;
  bool gameOver = false;
};
