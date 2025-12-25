#include "AppVoice.h"
#include "DisplayService.h"
#include "InputService.h"

AppVoice::AppVoice(NetService& net, MicInService& mic, AudioOutService& audio)
  : netService(net), micIn(mic), audioOut(audio) {}

void AppVoice::onEnter() {
  state = netService.isConnected() ? STATE_READY : STATE_BACKEND_OFF;
  micIn.setMode(MIC_OFF);
}

void AppVoice::onExit() {
  micIn.setMode(MIC_OFF);
  audioOut.stop();
}

void AppVoice::handleInput(InputService& input) {
  if (state == STATE_BACKEND_OFF) return;

  if (input.pressed(BTN_A)) {
    state = STATE_LISTENING;
    micIn.setMode(MIC_BACKEND_STREAM);
    audioOut.playSfx(SFX_START);
  } else if (input.released(BTN_A)) {
    if (state == STATE_LISTENING) {
      micIn.setMode(MIC_OFF);
      state = STATE_READY;
      audioOut.playSfx(SFX_CLICK);
    }
  }
}

void AppVoice::tick(unsigned long) {
  if (!netService.isConnected()) {
    state = STATE_BACKEND_OFF;
  }
}

void AppVoice::render(DisplayService& display) {
  display.drawText(0, 0, "VOICE", 1);

  if (state == STATE_BACKEND_OFF) {
    display.drawCentered("BACKEND OFF", 24, 1);
    display.drawCentered("CONNECT WIFI", 36, 1);
  } else if (state == STATE_READY) {
    display.drawCentered("READY", 28, 2);
    display.drawText(0, 56, "Hold A to talk", 1);
  } else if (state == STATE_LISTENING) {
    display.drawCentered("LISTENING", 28, 2);
  } else if (state == STATE_PLAYING) {
    display.drawCentered("PLAYING", 28, 2);
  }
}
