#pragma once

#include "Screen.h"
#include "AudioOutService.h"
#include "NetService.h"
#include "ScreenManager.h"

class AppSettings : public Screen {
public:
  struct WifiPreset {
    const char* name;
    const char* ssid;
    const char* pass;
  };

  AppSettings(AudioOutService& audio, NetService& net, ScreenManager& screens);
  void onEnter() override;
  void handleInput(InputService& input) override;
  void render(DisplayService& display) override;

private:
  AudioOutService& audioOut;
  NetService& netService;
  ScreenManager& screenManager;

  int volumePercent = 35;
  bool muted = false;
  int wifiIndex = 0;
};
