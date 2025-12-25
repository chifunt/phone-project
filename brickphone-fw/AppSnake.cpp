#include "AppSnake.h"
#include "DisplayService.h"
#include "InputService.h"

AppSnake::AppSnake(AudioOutService& audio) : audioOut(audio) {}

void AppSnake::onEnter() {
  resetGame();
  running = true;
  gameOver = false;
  lastStepMs = millis();
}

void AppSnake::handleInput(InputService& input) {
  if (input.pressed(BTN_A)) {
    soundEnabled = !soundEnabled;
  }
  if (input.pressed(BTN_SELECT)) {
    fastMode = !fastMode;
    stepIntervalMs = fastMode ? 70 : 120;
  }
  if (input.pressed(BTN_B)) {
    resetGame();
    running = true;
    gameOver = false;
  }

  if (input.pressed(BTN_UP)) nextDir = DIR_UP;
  else if (input.pressed(BTN_DOWN)) nextDir = DIR_DOWN;
  else if (input.pressed(BTN_LEFT)) nextDir = DIR_LEFT;
  else if (input.pressed(BTN_RIGHT)) nextDir = DIR_RIGHT;
}

void AppSnake::tick(unsigned long) {
  if (!running || gameOver) return;

  unsigned long now = millis();
  if (now - lastStepMs < stepIntervalMs) return;
  lastStepMs = now;

  if (!isOpposite(dir, nextDir)) dir = nextDir;

  Pt head = snake[0];
  switch (dir) {
    case DIR_UP:    head.y = (head.y == 0) ? (GRID_H - 1) : head.y - 1; break;
    case DIR_DOWN:  head.y = (head.y + 1) % GRID_H; break;
    case DIR_LEFT:  head.x = (head.x == 0) ? (GRID_W - 1) : head.x - 1; break;
    case DIR_RIGHT: head.x = (head.x + 1) % GRID_W; break;
  }

  if (contains(head.x, head.y)) {
    gameOver = true;
    if (soundEnabled) audioOut.playSfx(SFX_OVER);
    return;
  }

  for (int i = snakeLen - 1; i > 0; --i) snake[i] = snake[i - 1];
  snake[0] = head;

  if (head.x == food.x && head.y == food.y) {
    if (snakeLen < MAX_CELLS - 1) snakeLen++;
    score++;
    spawnFood();
    if (soundEnabled) audioOut.playSfx(SFX_EAT);
  }
}

void AppSnake::render(DisplayService& display) {
  for (int i = 0; i < snakeLen; ++i) {
    int16_t x = snake[i].x * CELL;
    int16_t y = snake[i].y * CELL;
    display.fillRect(x, y, CELL, CELL);
  }

  display.drawRect(food.x * CELL, food.y * CELL, CELL, CELL);

  char buf[16];
  snprintf(buf, sizeof(buf), "S:%d", score);
  display.drawText(0, 0, buf, 1);

  if (gameOver) {
    display.drawCentered("GAME OVER", 24, 2);
  }
}

void AppSnake::resetGame() {
  score = 0;
  snakeLen = 3;

  uint8_t sx = GRID_W / 2;
  uint8_t sy = GRID_H / 2;
  snake[0] = { sx, sy };
  snake[1] = { (uint8_t)(sx - 1), sy };
  snake[2] = { (uint8_t)(sx - 2), sy };

  dir = DIR_RIGHT;
  nextDir = DIR_RIGHT;
  spawnFood();
}

void AppSnake::spawnFood() {
  for (int tries = 0; tries < 2000; ++tries) {
    uint8_t x = (uint8_t)random(0, GRID_W);
    uint8_t y = (uint8_t)random(0, GRID_H);
    if (!contains(x, y)) {
      food = { x, y };
      return;
    }
  }
  food = { (uint8_t)(GRID_W / 2), (uint8_t)(GRID_H / 2) };
}

bool AppSnake::contains(uint8_t x, uint8_t y) {
  for (int i = 0; i < snakeLen; ++i) {
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

bool AppSnake::isOpposite(Dir a, Dir b) {
  return (a == DIR_UP && b == DIR_DOWN) ||
         (a == DIR_DOWN && b == DIR_UP) ||
         (a == DIR_LEFT && b == DIR_RIGHT) ||
         (a == DIR_RIGHT && b == DIR_LEFT);
}
