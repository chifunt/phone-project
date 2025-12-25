#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class DisplayService {
public:
  void begin();
  void beginFrame();
  void endFrame();
  void setOffset(int8_t x, int8_t y);

  void clear();
  void drawText(int16_t x, int16_t y, const char* text, uint8_t size);
  void drawCentered(const char* text, int16_t y, uint8_t size);
  void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h);

  int16_t width() const { return 128; }
  int16_t height() const { return 64; }

private:
  Adafruit_SSD1306 display{128, 64, &Wire, -1};
  int8_t offsetX = 0;
  int8_t offsetY = 0;
};
