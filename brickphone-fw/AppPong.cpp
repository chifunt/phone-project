#include "AppPong.h"
#include "DisplayService.h"
#include "InputService.h"

static const int kPaddleH = 16;
static const int kPaddleW = 3;
static const int kBallSize = 2;

AppPong::AppPong(AudioOutService& audio) : audioOut(audio) {}

void AppPong::onEnter() {
  ballX = 64;
  ballY = 32;
  ballVX = 1;
  ballVY = 1;
  paddleY = 24;
  aiY = 24;
  paused = false;
  lastStepMs = millis();
}

void AppPong::handleInput(InputService& input) {
  if (input.pressed(BTN_A)) paused = !paused;
  if (input.pressed(BTN_B)) {
    playerScore = 0;
    aiScore = 0;
  }

  moveDir = 0;
  if (input.down(BTN_UP)) moveDir = -1;
  else if (input.down(BTN_DOWN)) moveDir = 1;
}

void AppPong::tick(unsigned long) {
  if (paused) return;

  unsigned long now = millis();
  if (now - lastStepMs < stepIntervalMs) return;
  lastStepMs = now;

  ballX += ballVX;
  ballY += ballVY;

  if (moveDir != 0) {
    paddleY += moveDir * 2;
    if (paddleY < 0) paddleY = 0;
    if (paddleY > 64 - kPaddleH) paddleY = 64 - kPaddleH;
  }

  if (ballY <= 0 || ballY >= 64 - kBallSize) {
    ballVY = -ballVY;
    audioOut.playSfx(SFX_CLICK);
  }

  // Player paddle (left)
  if (ballX <= kPaddleW + 2) {
    if (ballY + kBallSize >= paddleY && ballY <= paddleY + kPaddleH) {
      ballVX = 1;
      audioOut.playSfx(SFX_CLICK);
    } else {
      aiScore++;
      ballX = 64;
      ballY = 32;
      ballVX = 1;
      ballVY = 1;
      audioOut.playSfx(SFX_OVER);
    }
  }

  // AI paddle (right)
  if (ballX >= 128 - kPaddleW - 2) {
    if (ballY + kBallSize >= aiY && ballY <= aiY + kPaddleH) {
      ballVX = -1;
      audioOut.playSfx(SFX_CLICK);
    } else {
      playerScore++;
      ballX = 64;
      ballY = 32;
      ballVX = -1;
      ballVY = 1;
      audioOut.playSfx(SFX_START);
    }
  }

  // Simple AI follow
  if (ballY < aiY) aiY -= 1;
  if (ballY > aiY + kPaddleH) aiY += 1;
  if (aiY < 0) aiY = 0;
  if (aiY > 64 - kPaddleH) aiY = 64 - kPaddleH;
}

void AppPong::render(DisplayService& display) {
  display.fillRect(0, paddleY, kPaddleW, kPaddleH);
  display.fillRect(128 - kPaddleW, aiY, kPaddleW, kPaddleH);
  display.fillRect(ballX, ballY, kBallSize, kBallSize);

  char score[12];
  snprintf(score, sizeof(score), "%d-%d", playerScore, aiScore);
  display.drawText(52, 0, score, 1);

  if (paused) {
    display.drawCentered("PAUSED", 24, 2);
  }
}
