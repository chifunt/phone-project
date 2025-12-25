#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 4
#define SCL_PIN 5

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int16_t offset = SCREEN_WIDTH;

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      while (true) delay(1000);
    }
  }
}

void loop() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(offset, (SCREEN_HEIGHT / 2) - 8);
  display.print("PCC TUI PHONE PROJECT SKIBIDI");

  display.display();

  offset -= 2;
  int16_t textWidth = 8 * 2 * 8; // rough width for "ESP32 S3" at size 2
  if (offset < -textWidth) offset = SCREEN_WIDTH;

  delay(20);
}
