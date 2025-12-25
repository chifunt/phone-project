#include "App2048.h"
#include "DisplayService.h"
#include "InputService.h"

App2048::App2048(AudioOutService& audio) : audioOut(audio) {}

void App2048::onEnter() {
  reset();
}

void App2048::handleInput(InputService& input) {
  bool moved = false;
  if (input.pressed(BTN_UP)) moved = move(0, -1);
  else if (input.pressed(BTN_DOWN)) moved = move(0, 1);
  else if (input.pressed(BTN_LEFT)) moved = move(-1, 0);
  else if (input.pressed(BTN_RIGHT)) moved = move(1, 0);

  if (moved) {
    spawnTile();
    audioOut.playSfx(SFX_CLICK);
    if (!hasMoves()) {
      gameOver = true;
      audioOut.playSfx(SFX_OVER);
    }
  }

  if (input.pressed(BTN_A)) reset();
}

void App2048::tick(unsigned long) {
}

void App2048::render(DisplayService& display) {
  const int cell = 15;
  const int x0 = 2;
  const int y0 = 2;

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      int x = x0 + c * cell;
      int y = y0 + r * cell;
      display.drawRect(x, y, cell, cell);
      if (grid[r][c] != 0) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%u", grid[r][c]);
        display.drawText(x + 2, y + 4, buf, 1);
      }
    }
  }

  if (won) display.drawCentered("2048!", 50, 1);
  if (gameOver) display.drawCentered("GAME OVER", 50, 1);
  display.drawText(0, 56, "A reset", 1);
}

void App2048::reset() {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      grid[r][c] = 0;
    }
  }
  won = false;
  gameOver = false;
  spawnTile();
  spawnTile();
}

bool App2048::move(int dx, int dy) {
  if (gameOver) return false;
  bool moved = false;
  bool merged[4][4] = {};

  int startX = (dx > 0) ? 3 : 0;
  int startY = (dy > 0) ? 3 : 0;
  int endX = (dx > 0) ? -1 : 4;
  int endY = (dy > 0) ? -1 : 4;
  int stepX = (dx > 0) ? -1 : 1;
  int stepY = (dy > 0) ? -1 : 1;

  for (int y = startY; y != endY; y += stepY) {
    for (int x = startX; x != endX; x += stepX) {
      if (grid[y][x] == 0) continue;
      int nx = x;
      int ny = y;
      while (true) {
        int tx = nx + dx;
        int ty = ny + dy;
        if (tx < 0 || tx >= 4 || ty < 0 || ty >= 4) break;
        if (grid[ty][tx] == 0) {
          grid[ty][tx] = grid[ny][nx];
          grid[ny][nx] = 0;
          nx = tx;
          ny = ty;
          moved = true;
          continue;
        }
        if (grid[ty][tx] == grid[ny][nx] && !merged[ty][tx]) {
          grid[ty][tx] *= 2;
          if (grid[ty][tx] == 2048) won = true;
          grid[ny][nx] = 0;
          merged[ty][tx] = true;
          moved = true;
        }
        break;
      }
    }
  }
  return moved;
}

bool App2048::spawnTile() {
  int empty[16];
  int count = 0;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (grid[r][c] == 0) {
        empty[count++] = r * 4 + c;
      }
    }
  }
  if (count == 0) return false;
  int pick = empty[random(count)];
  int r = pick / 4;
  int c = pick % 4;
  grid[r][c] = (random(10) == 0) ? 4 : 2;
  return true;
}

bool App2048::hasMoves() const {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (grid[r][c] == 0) return true;
      if (r < 3 && grid[r][c] == grid[r + 1][c]) return true;
      if (c < 3 && grid[r][c] == grid[r][c + 1]) return true;
    }
  }
  return false;
}
