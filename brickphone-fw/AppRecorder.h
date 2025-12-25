#pragma once

#include "Screen.h"
#include "MicInService.h"
#include "AudioOutService.h"
#include "Pins.h"

class AppRecorder : public Screen {
public:
  AppRecorder(MicInService& mic, AudioOutService& audio);
  void onEnter() override;
  void onExit() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  void startRecording();
  void stopRecording();
  void startPlayback();
  void clearRecording();

  MicInService& micIn;
  AudioOutService& audioOut;

  static const int kMaxSeconds = 3;
  static const int kMaxFrames = AUDIO_SAMPLE_RATE * kMaxSeconds;
  int16_t buffer[kMaxFrames];
  int framesRecorded = 0;
  bool recording = false;
};
