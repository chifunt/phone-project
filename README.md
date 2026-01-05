# Brickphone Firmware

ESP32-S3 handheld with a 128x64 SSD1306 OLED, 8 Game Boy-style buttons, I2S speaker amp, and I2S microphone. This repo contains the main firmware, a working voice client sketch, a Cloudflare Workers backend, and hardware bring-up tests.

## Firmware Layout
- `brickphone-fw/brickphone-fw.ino` — main firmware entry
- `brickphone-fw/` — services, screen framework, and apps
- `brickphone-fw-voiceclient/` — standalone voice client sketch (known working)
- `voice-backend/` — Cloudflare Workers backend + protocol spec
- `hardware-tests/` — focused sketches for individual hardware tests

## Arduino IDE Settings
- Board: ESP32S3 Dev Module
- USB CDC On Boot: Enabled
- Flash Size: 16MB

## Apps (Current)
Order reflects the in-device menu.
- Voice (live WebSocket client using `brickphone-fw/secrets.h`)
- Recorder
- Snake
- Pong
- Breakout
- Space Invaders
- 2048
- Flappy Bird
- Settings

## Controls
Global:
- START: return to menu
- A: confirm / primary
- B: back / secondary
- SELECT: app-specific
- D-Pad: navigate

Per-app:
- Snake: A sound toggle, SELECT speed toggle, B reset
- Recorder: A record, B play, SELECT clear
- Voice: hold A to talk (WebSocket streaming to backend)
- Pong: A pause, B reset, UP/DOWN move
- Breakout: A launch, B reset, LEFT/RIGHT move
- Space Invaders: A shoot, B reset, LEFT/RIGHT move
- 2048: D-Pad move, A reset
- Flappy: A flap / retry
- Settings: UP/DOWN volume, LEFT/RIGHT Wi-Fi preset, SELECT connect, A mute, B back

## Hardware
- MCU: ESP32-S3
- Display: 128x64 SSD1306 OLED over I2C
- Speaker amp: MAX98357 I2S DAC/amp
- Mic: I2S mic (tested with INMP441-style wiring)
- Buttons: 8 inputs, Game Boy layout

## Pin Map
Buttons (INPUT_PULLUP, pressed = LOW):
- RIGHT: GPIO 35
- UP: GPIO 36
- DOWN: GPIO 37
- LEFT: GPIO 38
- A: GPIO 39
- B: GPIO 40
- SELECT: GPIO 41
- START: GPIO 42

OLED (SSD1306, I2C):
- SDA: GPIO 4
- SCL: GPIO 5
- Address: 0x3C (fallback 0x3D in tests)

Speaker (MAX98357, I2S TX @ 24 kHz):
- DIN: GPIO 9
- BCLK: GPIO 10
- LRC/WS: GPIO 11

Microphone (I2S RX @ 24 kHz):
- SD: GPIO 16
- BCLK: GPIO 17
- WS/LRCL: GPIO 18

## Audio Standard
- Sample rate: 24 kHz
- Format: PCM16, mono
- Goal: keep input/output aligned for future voice features

## Libraries
- Arduino core for ESP32
- `Wire` (I2C)
- `Adafruit_GFX`
- `Adafruit_SSD1306`
- ESP-IDF I2S driver: `driver/i2s.h`
- Python (Mac audio playback): `pyserial`, `sounddevice`
- `WebSocketsClient` (Links2004)

## Hardware Tests
- `hardware-tests/sketch-buttons-working/sketch-buttons-working.ino`
- `hardware-tests/sketch-ssd1306-working/sketch-ssd1306-working.ino`
- `hardware-tests/sketch-max-speaker-working/max-speaker-working.ino`
- `hardware-tests/sketch-micinmp-working/sketch-micinmp-working.ino`
- `hardware-tests/sketch-mic-to-speaker/sketch-mic-to-speaker.ino`
- `hardware-tests/sketch-snakegame-sampleproject/sketch-snakegame-sampleproject.ino`

## Mic Streaming to Mac
The mic test streams raw 16-bit PCM mono at 24 kHz over USB serial. `pyplayer.py` plays it live.

1) Flash `hardware-tests/sketch-micinmp-working/sketch-micinmp-working.ino`
2) Edit `PORT` in `hardware-tests/pyplayer.py`
3) Install deps (if needed): `pip3 install pyserial sounddevice`
4) Run: `./hardware-tests/pyplayer.py`

## Secrets (Local Only)
Create `brickphone-fw/secrets.h` (gitignored) to store Wi-Fi and voice backend auth:

```
#pragma once
#define WIFI_SSID_STR "YOUR_SSID"
#define WIFI_PASS_STR "YOUR_PASS"
#define BRICKPHONE_TOKEN "YOUR_TOKEN_HERE"
```

The Voice app uses these values directly for Wi‑Fi + auth.

## Voice Backend
- Protocol spec: `voice-backend/README.md`
- Worker: `voice-backend/worker.ts` (expects `BRICKPHONE_TOKEN` and `OPENAI_API_KEY`)

## Notes
- Buttons use `INPUT_PULLUP` (wire to GND when pressed).
- MAX98357 SD pin should be tied to 3V3.
- If you change pins, update them in `brickphone-fw/Pins.h`.

## Known Gaps / Placeholders
- Settings Wi‑Fi presets are placeholders (`YOUR_HOME_SSID`, etc.) and do not drive the Voice app.
- NetService is currently Wi‑Fi only; voice streaming is handled inside `AppVoice`.

## Roadmap
- Wi-Fi preset selection UI polish and backend transport for Voice
- Save data in NVS (high scores)
- Audio refinements (SFX polish, mix/ducking)
- Bad Apple video playback via external storage or dedicated data partition
