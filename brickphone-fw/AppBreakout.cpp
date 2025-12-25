#include "AppBreakout.h"
#include "DisplayService.h"
#include "InputService.h"

static const int kPaddleW = 24;
static const int kPaddleH = 3;
static const int kBallSize = 2;

AppBreakout::AppBreakout(AudioOutService& audio) : audioOut(audio) {}

void AppBreakout::onEnter() {
  reset();
  lastStepMs = millis();
}

void AppBreakout::handleInput(InputService& input) {
  moveDir = 0;
  if (input.down(BTN_LEFT)) moveDir = -1;
  else if (input.down(BTN_RIGHT)) moveDir = 1;

  if (input.pressed(BTN_A)) {
    if (won) reset();
    launched = true;
  }
  if (input.pressed(BTN_B)) reset();
}

void AppBreakout::tick(unsigned long) {
  if (!launched) {
    ballX = paddleX + (kPaddleW / 2);
    ballY = 54;
    return;
  }

  if (won) return;

  unsigned long now = millis();
  if (now - lastStepMs < stepIntervalMs) return;
  lastStepMs = now;

  ballX += ballVX;
  ballY += ballVY;

  if (moveDir != 0) {
    paddleX += moveDir * 2;
    if (paddleX < 0) paddleX = 0;
    if (paddleX > 128 - kPaddleW) paddleX = 128 - kPaddleW;
  }

  if (ballX <= 0 || ballX >= 128 - kBallSize) {
    ballVX = -ballVX;
    audioOut.playSfx(SFX_CLICK);
  }
  if (ballY <= 0) {
    ballVY = -ballVY;
    audioOut.playSfx(SFX_CLICK);
  }

  if (ballY >= 64 - kPaddleH - kBallSize) {
    if (ballX + kBallSize >= paddleX && ballX <= paddleX + kPaddleW) {
      ballVY = -ballVY;
      audioOut.playSfx(SFX_CLICK);
    } else {
      reset();
      audioOut.playSfx(SFX_OVER);
      return;
    }
  }

  // Brick collisions
  bool anyLeft = false;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (!bricks[r][c]) continue;
      anyLeft = true;
      int bx = 4 + c * kBrickW;
      int by = 8 + r * kBrickH;
      if (ballX + kBallSize >= bx && ballX <= bx + kBrickW &&
          ballY + kBallSize >= by && ballY <= by + kBrickH) {
        bricks[r][c] = false;
        ballVY = -ballVY;
        audioOut.playSfx(SFX_EAT);
        return;
      }
    }
  }

  if (!anyLeft) {
    won = true;
    launched = false;
    audioOut.playSfx(SFX_START);
  }
}

void AppBreakout::render(DisplayService& display) {
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (bricks[r][c]) {
        int bx = 4 + c * kBrickW;
        int by = 8 + r * kBrickH;
        display.fillRect(bx, by, kBrickW - 2, kBrickH - 2);
      }
    }
  }

  display.fillRect(paddleX, 60, kPaddleW, kPaddleH);
  display.fillRect(ballX, ballY, kBallSize, kBallSize);

  if (won) {
    display.drawCentered("YOU WIN", 24, 2);
    display.drawText(0, 56, "A restart", 1);
  } else if (!launched) {
    display.drawText(0, 0, "A launch  B reset", 1);
  }
}

void AppBreakout::reset() {
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      bricks[r][c] = true;
    }
  }
  ballX = 64;
  ballY = 40;
  ballVX = 1;
  ballVY = -1;
  paddleX = 50;
  launched = false;
  won = false;
}
