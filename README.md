# ESP32 Mini Console Project

Small ESP32-based handheld with a 128x64 OLED, 8 Game Boy-style buttons, I2S speaker amp, and I2S microphone. Each `sketch-*` folder is a focused hardware test. The `sketch-snakegame-sampleproject` combines most features (buttons, display, audio), and the microphone test streams raw audio to a Mac via USB serial.

## Hardware
- MCU: ESP32-S3 (I2S + USB serial used; adjust pins if your board differs)
- Display: 128x64 SSD1306 OLED over I2C
- Speaker amp: MAX98357 I2S DAC/amp
- Mic: I2S mic (tested with INMP441-style wiring)
- Buttons: 8 inputs, Game Boy layout

## Pin Map
All pins below are from the sketches and are easy to change in code.

### Buttons (with INPUT_PULLUP)
- RIGHT: GPIO 35
- UP: GPIO 36
- DOWN: GPIO 37
- LEFT: GPIO 38
- A: GPIO 39
- B: GPIO 40
- SELECT: GPIO 41
- START: GPIO 42

### OLED (SSD1306, I2C)
- SDA: GPIO 4
- SCL: GPIO 5
- Address: 0x3C (fallback to 0x3D in test sketch)

### Speaker (MAX98357, I2S TX @ 24 kHz)
- DIN: GPIO 9
- BCLK: GPIO 10
- LRC/WS: GPIO 11

### Microphone (I2S RX @ 24 kHz)
- SD (data out from mic): GPIO 16
- BCLK: GPIO 17
- WS/LRCL: GPIO 18

## Libraries and Frameworks
- Arduino core for ESP32
- `Wire` (I2C)
- `Adafruit_GFX`
- `Adafruit_SSD1306`
- ESP-IDF I2S driver: `driver/i2s.h`
- Python (Mac audio playback): `pyserial`, `sounddevice`

## Sketches
- `sketch-buttons-working/sketch-buttons-working.ino`
  - Reads 8 buttons with debounce and prints presses/releases over Serial.
- `sketch-ssd1306-working/sketch-ssd1306-working.ino`
  - SSD1306 bring-up and simple scrolling text.
- `sketch-max-speaker-working/max-speaker-working.ino`
  - I2S audio output to MAX98357; plays a simple melody.
- `sketch-micinmp-working/sketch-micinmp-working.ino`
  - I2S mic input at 16 kHz; streams raw 16-bit mono PCM over Serial.
- `sketch-snakegame-sampleproject/sketch-snakegame-sampleproject.ino`
  - Snake game with OLED, buttons, and I2S audio SFX. Mic not integrated yet.

## Mic Streaming to Mac
The mic sketch outputs raw 16-bit PCM mono at 24 kHz over USB serial. `pyplayer.py` plays it live.

1) Flash `sketch-micinmp-working/sketch-micinmp-working.ino`
2) Edit `PORT` in `pyplayer.py` to match your device (ex: `/dev/cu.usbmodem101`)
3) Install deps (if needed):
   - `pip3 install pyserial sounddevice`
4) Run:
   - `./pyplayer.py`

## Notes
- Buttons use `INPUT_PULLUP`, so wire buttons to GND when pressed.
- MAX98357 SD pin should be tied to 3V3 to keep it enabled.
- If you change pins, update them in each sketch to match your wiring.

## Roadmap
- Boot-up sound (Nokia-style jingle) + splash animation.
- Start screen with logo and version info.
- Main menu with apps (Snake, Recorder, Settings).
- Button remap and long-press shortcuts (e.g., Start = menu).
- Settings UI (volume, brightness, mic gain).
- Save data in NVS (high scores, last app).
- Voice recorder with waveform view + playback.
- Wi-Fi setup/config screen (no time sync needed).
- Refine audio (mixer, volume UI, more SFX/music).
- Add more games (Pong, Breakout, or small UI demos).
