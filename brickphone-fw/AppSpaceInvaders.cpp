#include "AppSpaceInvaders.h"
#include "DisplayService.h"
#include "InputService.h"

static const uint8_t kInvaderA[] PROGMEM = {
  0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5, 0x42
};

static const uint8_t kInvaderB[] PROGMEM = {
  0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x18, 0x5A, 0xA5
};

AppSpaceInvaders::AppSpaceInvaders(AudioOutService& audio) : audioOut(audio) {}

void AppSpaceInvaders::onEnter() {
  reset();
  lastStepMs = millis();
  lastPlayerStepMs = lastStepMs;
}

void AppSpaceInvaders::handleInput(InputService& input) {
  moveDir = 0;
  if (input.down(BTN_LEFT)) moveDir = -1;
  else if (input.down(BTN_RIGHT)) moveDir = 1;

  if (input.pressed(BTN_A) && !bulletActive && !won && !lost) {
    bulletActive = true;
    bulletX = playerX + 4;
    bulletY = 56;
    audioOut.playSfx(SFX_CLICK);
  }

  if (input.pressed(BTN_B)) reset();
}

void AppSpaceInvaders::tick(unsigned long) {
  if (won || lost) return;

  unsigned long now = millis();
  if (now - lastStepMs >= stepIntervalMs) {
    lastStepMs = now;
    swarmX += swarmDir * 2;
    int swarmWidth = kCols * kAlienW;
    if (swarmX <= 0 || swarmX + swarmWidth >= 128) {
      swarmDir = -swarmDir;
      swarmY += 4;
      audioOut.playSfx(SFX_CLICK);
    }
    animFrame = !animFrame;
  }

  if (moveDir != 0 && now - lastPlayerStepMs >= playerStepIntervalMs) {
    lastPlayerStepMs = now;
    playerX += moveDir * 2;
    if (playerX < 0) playerX = 0;
    if (playerX > 128 - 10) playerX = 128 - 10;
  }

  if (bulletActive) {
    bulletY -= 3;
    if (bulletY < 0) bulletActive = false;
  }

  bool anyLeft = false;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (!aliens[r][c]) continue;
      anyLeft = true;
      int ax = swarmX + c * kAlienW;
      int ay = swarmY + r * kAlienH;
      if (bulletActive &&
          bulletX >= ax && bulletX <= ax + kAlienW &&
          bulletY >= ay && bulletY <= ay + kAlienH) {
        aliens[r][c] = false;
        bulletActive = false;
        audioOut.playSfx(SFX_EAT);
      }
      if (ay + kAlienH >= 54) {
        lost = true;
        audioOut.playSfx(SFX_OVER);
      }
    }
  }

  if (!anyLeft) {
    won = true;
    audioOut.playSfx(SFX_START);
  }
}

void AppSpaceInvaders::render(DisplayService& display) {
  const uint8_t* sprite = animFrame ? kInvaderA : kInvaderB;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (!aliens[r][c]) continue;
      int ax = swarmX + c * kAlienW;
      int ay = swarmY + r * kAlienH;
      display.drawBitmap(ax, ay, sprite, kAlienW, kAlienH);
    }
  }

  display.fillRect(playerX, 58, 10, 3);

  if (bulletActive) {
    display.fillRect(bulletX, bulletY, 2, 3);
  }

  if (won) {
    display.drawCentered("YOU WIN", 24, 2);
    display.drawText(0, 56, "B reset", 1);
  } else if (lost) {
    display.drawCentered("GAME OVER", 24, 2);
    display.drawText(0, 56, "B reset", 1);
  }
}

void AppSpaceInvaders::reset() {
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      aliens[r][c] = true;
    }
  }
  swarmX = 8;
  swarmY = 10;
  swarmDir = 1;
  playerX = 56;
  bulletActive = false;
  moveDir = 0;
  animFrame = false;
  won = false;
  lost = false;
}
