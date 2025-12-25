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

  const uint8_t visibleRows = 3;
  if (selected < scroll) scroll = selected;
  if (selected >= scroll + visibleRows) scroll = selected - (visibleRows - 1);
}

void MenuScreen::render(DisplayService& display) {
  display.drawText(0, 0, "BRICKPHONE", 1);

  const int16_t listTop = 12;
  const int16_t rowH = 14;
  const uint8_t visibleRows = 3;

  for (uint8_t i = 0; i < visibleRows; ++i) {
    uint8_t idx = scroll + i;
    if (idx >= kEntryCount) break;
    int16_t y = listTop + (i * rowH);
    if (idx == selected) {
      display.drawBitmap(0, y + 3, ICON_ARROW_8X8, 8, 8);
    }
    display.drawBitmap(10, y, kEntries[idx].icon, 16, 16);
    display.drawText(28, y + 4, kEntries[idx].label, 1);
  }

  // Scrollbar
  int16_t barX = 122;
  int16_t barY = listTop;
  int16_t barH = rowH * visibleRows;
  display.drawRect(barX, barY, 4, barH);
  int16_t thumbH = (barH * visibleRows) / kEntryCount;
  if (thumbH < 4) thumbH = 4;
  int16_t maxOffset = kEntryCount - visibleRows;
  int16_t thumbY = barY;
  if (maxOffset > 0) {
    thumbY = barY + (barH - thumbH) * scroll / maxOffset;
  }
  display.fillRect(barX + 1, thumbY + 1, 2, thumbH - 2);

  display.drawText(0, 56, "A select  START menu", 1);
}
