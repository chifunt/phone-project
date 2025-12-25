#include "DisplayService.h"
#include "Pins.h"
#include <Wire.h>

void DisplayService::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR_MAIN)) {
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR_ALT);
  }
  display.clearDisplay();
  display.display();
}

void DisplayService::beginFrame() {
  display.clearDisplay();
}

void DisplayService::endFrame() {
  display.display();
}

void DisplayService::setOffset(int8_t x, int8_t y) {
  offsetX = x;
  offsetY = y;
}

void DisplayService::clear() {
  display.clearDisplay();
}

void DisplayService::drawText(int16_t x, int16_t y, const char* text, uint8_t size) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x + offsetX, y + offsetY);
  display.print(text);
}

void DisplayService::drawCentered(const char* text, int16_t y, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x + offsetX, y + offsetY);
  display.print(text);
}

void DisplayService::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h) {
  display.drawBitmap(x + offsetX, y + offsetY, bitmap, w, h, SSD1306_WHITE);
}

void DisplayService::drawRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  display.drawRect(x + offsetX, y + offsetY, w, h, SSD1306_WHITE);
}

void DisplayService::fillRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  display.fillRect(x + offsetX, y + offsetY, w, h, SSD1306_WHITE);
}
