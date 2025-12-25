#include "AppFlappy.h"
#include "DisplayService.h"
#include "InputService.h"

static const int kPipeW = 12;
static const int kGapH = 18;
static const int kBirdX = 24;

AppFlappy::AppFlappy(AudioOutService& audio) : audioOut(audio) {}

void AppFlappy::onEnter() {
  reset();
  lastStepMs = millis();
}

void AppFlappy::handleInput(InputService& input) {
  if (input.pressed(BTN_A)) {
    if (dead) reset();
    birdV = -2.6f;
    audioOut.playSfx(SFX_CLICK);
  }
}

void AppFlappy::tick(unsigned long) {
  unsigned long now = millis();
  if (now - lastStepMs < stepIntervalMs) return;
  lastStepMs = now;

  if (dead) return;

  birdV += 0.18f;
  birdY += birdV;

  for (int i = 0; i < 2; ++i) {
    pipeX[i] -= 1;
    if (pipeX[i] + kPipeW < 0) {
      pipeX[i] = 128 + 30;
      pipeGapY[i] = 12 + random(28);
    }

    if (pipeX[i] + kPipeW == kBirdX) {
      score++;
      audioOut.playSfx(SFX_START);
    }
  }

  if (birdY < 0 || birdY > 63) {
    dead = true;
    audioOut.playSfx(SFX_OVER);
  }

  for (int i = 0; i < 2; ++i) {
    int px = pipeX[i];
    int gapY = pipeGapY[i];
    if (kBirdX >= px && kBirdX <= px + kPipeW) {
      if (birdY < gapY || birdY > gapY + kGapH) {
        dead = true;
        audioOut.playSfx(SFX_OVER);
      }
    }
  }
}

void AppFlappy::render(DisplayService& display) {
  for (int i = 0; i < 2; ++i) {
    int px = pipeX[i];
    int gapY = pipeGapY[i];
    display.fillRect(px, 0, kPipeW, gapY);
    display.fillRect(px, gapY + kGapH, kPipeW, 64 - (gapY + kGapH));
  }

  display.fillRect(kBirdX, (int)birdY, 4, 4);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", score);
  display.drawText(0, 0, buf, 1);

  if (dead) {
    display.drawCentered("GAME OVER", 24, 2);
    display.drawText(0, 56, "A retry", 1);
  }
}

void AppFlappy::reset() {
  birdY = 32.0f;
  birdV = 0.0f;
  pipeX[0] = 128;
  pipeX[1] = 128 + 60;
  pipeGapY[0] = 16;
  pipeGapY[1] = 28;
  score = 0;
  dead = false;
}
