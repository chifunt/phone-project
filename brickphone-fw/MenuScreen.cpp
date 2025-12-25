#include "MenuScreen.h"
#include "DisplayService.h"
#include "InputService.h"
#include "Icons.h"

static const MenuScreen::Entry kEntries[] = {
  { "Voice",   ICON_VOICE_16X16,   ScreenId::Voice },
  { "Snake",   ICON_SNAKE_16X16,   ScreenId::Snake },
  { "Recorder", ICON_REC_16X16,    ScreenId::Recorder },
  { "Settings", ICON_SETTINGS_16X16, ScreenId::Settings }
};

static const uint8_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

MenuScreen::MenuScreen(ScreenManager& screens, AudioOutService& audio)
  : screenManager(screens), audioOut(audio) {}

void MenuScreen::handleInput(InputService& input) {
  if (input.pressed(BTN_DOWN)) {
    selected = (selected + 1) % kEntryCount;
    audioOut.playSfx(SFX_CLICK);
  } else if (input.pressed(BTN_UP)) {
    selected = (selected == 0) ? (kEntryCount - 1) : (selected - 1);
    audioOut.playSfx(SFX_CLICK);
  } else if (input.pressed(BTN_A)) {
    audioOut.playSfx(SFX_START);
    screenManager.set(kEntries[selected].id);
  }
}

void MenuScreen::render(DisplayService& display) {
  display.drawText(0, 0, "BRICKPHONE", 1);

  for (uint8_t i = 0; i < kEntryCount; ++i) {
    int16_t y = 14 + (i * 12);
    if (i == selected) {
      display.drawBitmap(0, y + 2, ICON_ARROW_8X8, 8, 8);
    }
    display.drawBitmap(10, y, kEntries[i].icon, 16, 16);
    display.drawText(28, y + 4, kEntries[i].label, 1);
  }

  display.drawText(0, 56, "A select  START menu", 1);
}
