#pragma once

#include "Screen.h"
#include "NetService.h"
#include "MicInService.h"
#include "AudioOutService.h"

class AppVoice : public Screen {
public:
  AppVoice(NetService& net, MicInService& mic, AudioOutService& audio);
  void onEnter() override;
  void onExit() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  enum State {
    STATE_BACKEND_OFF = 0,
    STATE_READY,
    STATE_LISTENING,
    STATE_PLAYING
  };

  NetService& netService;
  MicInService& micIn;
  AudioOutService& audioOut;
  State state = STATE_BACKEND_OFF;
};
