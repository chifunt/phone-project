#pragma once

#include "Screen.h"
#include <WebSocketsClient.h>
#include "MicInService.h"
#include "AudioOutService.h"

class AppVoice : public Screen {
public:
  AppVoice(MicInService& mic, AudioOutService& audio);
  void onEnter() override;
  void onExit() override;
  void handleInput(InputService& input) override;
  void tick(unsigned long dtMs) override;
  void render(DisplayService& display) override;

private:
  enum UiState {
    UI_WIFI_CONNECTING = 0,
    UI_WS_CONNECTING,
    UI_READY,
    UI_STREAMING,
    UI_ERROR
  };

  void startWifi();
  void startWebSocket();
  void sendHello();
  void sendStart();
  void sendStop();
  void sendPing();
  void sendAudioFrame(bool startFlag, bool endFlag);
  void handleJson(const String& text);
  void handleBinary(const uint8_t* data, size_t len);
  void onWsEvent(WStype_t type, uint8_t* payload, size_t length);
  void setError(const char* msg);

  static void wsEventThunk(WStype_t type, uint8_t* payload, size_t length);

  MicInService& micIn;
  AudioOutService& audioOut;
  WebSocketsClient ws;

  UiState uiState = UI_WIFI_CONNECTING;
  bool wsReady = false;
  bool streaming = false;
  bool startPending = false;
  uint16_t txSeq = 0;
  unsigned long lastPingMs = 0;
  unsigned long lastWifiAttemptMs = 0;
  bool wsStarted = false;
  bool wifiLoggedUp = false;

  static const int FRAME_SAMPLES = 480;
  static const int FRAME_BYTES = FRAME_SAMPLES * 2;
  int16_t micPcm[FRAME_SAMPLES];
  uint8_t txFrame[12 + FRAME_BYTES];
  int16_t rxBuf[FRAME_SAMPLES];

  char errorMsg[64] = "";
};
