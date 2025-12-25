#include "ScreenManager.h"
#include "DisplayService.h"
#include "AudioOutService.h"

void ScreenManager::registerScreen(ScreenId id, Screen* screen) {
  screens[(int)id] = screen;
}

void ScreenManager::setAudio(AudioOutService* audio) {
  audioOut = audio;
}

void ScreenManager::set(ScreenId id) {
  if (currentScreen) currentScreen->onExit();
  current = id;
  currentScreen = screens[(int)id];
  if (currentScreen) currentScreen->onEnter();
}

void ScreenManager::tick(unsigned long dtMs, InputService& input) {
  if (!currentScreen) return;

  if (input.pressed(BTN_START) && current != ScreenId::Menu) {
    if (audioOut) audioOut->playSfx(SFX_CLICK);
    set(ScreenId::Menu);
    return;
  }

  currentScreen->handleInput(input);
  currentScreen->tick(dtMs);
}

void ScreenManager::render(DisplayService& display) {
  if (currentScreen) currentScreen->render(display);
}
