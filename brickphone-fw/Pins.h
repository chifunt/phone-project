#pragma once

// ---------------- BUTTONS ----------------
#define PIN_BTN_RIGHT  35
#define PIN_BTN_UP     36
#define PIN_BTN_DOWN   37
#define PIN_BTN_LEFT   38
#define PIN_BTN_A      39
#define PIN_BTN_B      40
#define PIN_BTN_SELECT 41
#define PIN_BTN_START  42

// ---------------- OLED (I2C) -------------
#define PIN_OLED_SDA   4
#define PIN_OLED_SCL   5
#define OLED_ADDR_MAIN 0x3C
#define OLED_ADDR_ALT  0x3D

// ---------------- I2S OUT (MAX98357) -----
#define PIN_I2S_OUT_DO   9
#define PIN_I2S_OUT_BCK  10
#define PIN_I2S_OUT_WS   11

// ---------------- I2S IN (MIC) ----------
#define PIN_I2S_IN_SD    16
#define PIN_I2S_IN_BCK   17
#define PIN_I2S_IN_WS    18

// ---------------- AUDIO STANDARD --------
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_FRAMES       512
