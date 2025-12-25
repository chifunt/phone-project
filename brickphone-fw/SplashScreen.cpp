#include "SplashScreen.h"
#include "DisplayService.h"
#include "InputService.h"

SplashScreen::SplashScreen(AudioOutService& audio, ScreenManager& screens)
  : audioOut(audio), screenManager(screens) {}

void SplashScreen::onEnter() {
  startMs = millis();
  skip = false;
  if (!played) {
    audioOut.playSfx(SFX_BOOT);
    played = true;
  }
}

void SplashScreen::handleInput(InputService& input) {
  if (input.pressed(BTN_A)) skip = true;
}

void SplashScreen::tick(unsigned long) {
  unsigned long elapsed = millis() - startMs;
  if (skip || elapsed > 2000) {
    screenManager.set(ScreenId::Menu);
  }
}

void SplashScreen::render(DisplayService& display) {
  display.drawCentered("BRICKPHONE", 18, 2);
  unsigned long t = millis() / 200;
  int16_t x = 40 + (t % 5) * 6;
  display.drawText(x, 44, ".", 2);
}
