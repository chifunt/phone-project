# Phone Project

Small collection of audio experiments for an ESP32-based phone prototype.

- `max-speaker-working/max-speaker-working.ino` drives a MAX98357 I2S amp and plays the Tetris (Korobeiniki) melody with a simple ADSR envelope and vibrato.
- `sketch-micinmp-working/sketch-micinmp-working.ino` captures audio from an INMP-style I2S microphone and streams 16-bit mono PCM over USB CDC/serial.
- `pyplayer.py` is a host-side helper that reads the raw PCM stream and plays it in real time via `sounddevice`.

## Python quick start

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Plug in the ESP32 running the microphone sketch, update `PORT` in `pyplayer.py` to match your serial device (e.g., `/dev/cu.usbmodem*`), then run:

```bash
python pyplayer.py
```

## Arduino notes

- Target board: ESP32-S3 (pin numbers and I2S peripherals are set accordingly).
- Speaker path: MAX98357 on `PIN_I2S_DO=9`, `PIN_I2S_BCK=10`, `PIN_I2S_WS=11`, 44.1 kHz stereo frames.
- Mic path: INMP-style mic on `I2S_BCLK=17`, `I2S_LRCL=18`, `I2S_DOUT=16`, 16 kHz mono.
- Build with the Arduino IDE or `arduino-cli`, selecting the appropriate ESP32-S3 board profile before flashing.
