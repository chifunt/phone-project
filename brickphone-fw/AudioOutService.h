#pragma once

#include <Arduino.h>

enum SfxId {
  SFX_BOOT = 0,
  SFX_CLICK,
  SFX_START,
  SFX_EAT,
  SFX_OVER
};

class AudioOutService {
public:
  void begin();
  void tick(unsigned long nowMs);
  void setVolume(float vol);
  void playToneMidi(int midi, int ms);
  void playSfx(SfxId id);
  int playPcm(const int16_t* pcm, int frames);
  int pcmFree() const;
  void stop();
  bool isPcmPlaying() const { return pcmCount > 0; }

private:
  struct Note {
    int midi;
    int ms;
  };

  void startSequence(const Note* seq, uint8_t len);
  bool advanceNote();
  void renderFrames(int frames);
  void renderPcmFrames(int frames);
  static void audioTaskThunk(void* arg);
  void audioTaskLoop();

  float volume = 0.2f;
  float phase = 0.0f;
  int noteSamplesLeft = 0;
  int noteTotalSamples = 0;
  int currentMidi = -1;

  const Note* sequence = nullptr;
  uint8_t sequenceLen = 0;
  uint8_t sequenceIndex = 0;
  bool playing = false;

  static const int PCM_RING_FRAMES = 2048;
  int16_t pcmRing[PCM_RING_FRAMES];
  volatile int pcmHead = 0;
  volatile int pcmTail = 0;
  volatile int pcmCount = 0;
  portMUX_TYPE pcmMux = portMUX_INITIALIZER_UNLOCKED;

  bool taskRunning = false;
  TaskHandle_t taskHandle = nullptr;
};
