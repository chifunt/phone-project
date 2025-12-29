#include "AppVoice.h"
#include "DisplayService.h"
#include "InputService.h"
#include "secrets.h"
#include <WiFi.h>
#include <string.h>
#include <esp_system.h>
#include <esp_bt.h>

static const char* WS_HOST = "phone-project.joshuatjhie.workers.dev";
static const char* WS_PATH = "/voice";
static const char* DEVICE_ID = "brick01";

static const unsigned long PING_INTERVAL_MS = 12000;

static AppVoice* gAppVoice = nullptr;

AppVoice::AppVoice(MicInService& mic, AudioOutService& audio)
  : micIn(mic), audioOut(audio) {}

void AppVoice::onEnter() {
  static bool btFreed = false;
  if (!btFreed) {
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    btFreed = true;
  }
  gAppVoice = this;
  streaming = false;
  wsReady = false;
  startPending = false;
  txSeq = 0;
  wsStarted = false;
  lastPingMs = millis();
  lastWifiAttemptMs = 0;
  wifiLoggedUp = false;
  errorMsg[0] = '\0';
  audioOut.shutdown(); // free memory for TLS handshake
  micIn.setMode(MIC_OFF);
  startWifi();
}

void AppVoice::onExit() {
  streaming = false;
  micIn.setMode(MIC_OFF);
  audioOut.begin(); // restore audio for other screens
  ws.disconnect();
  wsReady = false;
  wsStarted = false;
  if (gAppVoice == this) gAppVoice = nullptr;
}

void AppVoice::startWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    uiState = UI_WS_CONNECTING;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_STR, WIFI_PASS_STR);
  WiFi.setSleep(false);
  lastWifiAttemptMs = millis();
  uiState = UI_WIFI_CONNECTING;
}

void AppVoice::startWebSocket() {
  if (wsStarted) return;
  wsStarted = true;
  wsReady = false;
  ws.onEvent(wsEventThunk);
  ws.setReconnectInterval(2000);
  // Minimal setup to mirror standalone working sketch
  ws.beginSSL(WS_HOST, 443, WS_PATH);
  uiState = UI_WS_CONNECTING;
}

void AppVoice::sendHello() {
  String msg = String("{\"type\":\"hello\",\"device_id\":\"") + DEVICE_ID +
               String("\",\"auth\":\"") + BRICKPHONE_TOKEN +
               String("\",\"sample_rate\":24000,\"channels\":1}");
  ws.sendTXT(msg);
}

void AppVoice::sendStart() {
  ws.sendTXT("{\"type\":\"start\",\"mode\":\"voice\"}");
}

void AppVoice::sendStop() {
  ws.sendTXT("{\"type\":\"stop\"}");
}

void AppVoice::sendPing() {
  String msg = String("{\"type\":\"ping\",\"t\":") + String(millis()) + "}";
  ws.sendTXT(msg);
}

void AppVoice::sendAudioFrame(bool startFlag, bool endFlag) {
  // Header: magic, version, flags, seq, samples, timestamp
  txFrame[0] = 0xB1;
  txFrame[1] = 0xA0;
  txFrame[2] = 0x01;
  uint8_t flags = 0;
  if (startFlag) flags |= 0x01;
  if (endFlag) flags |= 0x02;
  txFrame[3] = flags;
  txFrame[4] = (uint8_t)(txSeq & 0xFF);
  txFrame[5] = (uint8_t)((txSeq >> 8) & 0xFF);
  txFrame[6] = (uint8_t)(FRAME_SAMPLES & 0xFF);
  txFrame[7] = (uint8_t)((FRAME_SAMPLES >> 8) & 0xFF);
  uint32_t ts = millis();
  txFrame[8] = (uint8_t)(ts & 0xFF);
  txFrame[9] = (uint8_t)((ts >> 8) & 0xFF);
  txFrame[10] = (uint8_t)((ts >> 16) & 0xFF);
  txFrame[11] = (uint8_t)((ts >> 24) & 0xFF);
  txSeq = (uint16_t)(txSeq + 1);

  memcpy(txFrame + 12, micPcm, FRAME_BYTES);
  ws.sendBIN(txFrame, sizeof(txFrame));
}

void AppVoice::handleJson(const String& text) {
  if (text.indexOf("\"type\":\"ready\"") >= 0) {
    wsReady = true;
    uiState = UI_READY;
    audioOut.begin(); // re-init audio once WS is ready
  }
  if (text.indexOf("\"type\":\"error\"") >= 0) {
    setError(text.c_str());
  }
}

void AppVoice::handleBinary(const uint8_t* data, size_t len) {
  if (len < 12) return;
  uint16_t magic = data[0] | (data[1] << 8);
  uint8_t version = data[2];
  uint16_t samples = data[6] | (data[7] << 8);
  size_t expected = 12 + samples * 2;
  if (magic != 0xA0B1 || version != 1 || expected != len) return;
  const int16_t* pcm = reinterpret_cast<const int16_t*>(data + 12);

  // Queue into mono PCM ring; drop if full
  int remaining = samples;
  int offset = 0;
  while (remaining > 0) {
    int freeFrames = audioOut.pcmFree();
    if (freeFrames <= 0) break;
    int chunk = remaining < freeFrames ? remaining : freeFrames;
    int queued = audioOut.playPcm(pcm + offset, chunk);
    remaining -= queued;
    offset += queued;
  }
  if (samples > 0 && uiState != UI_STREAMING && uiState != UI_ERROR) {
    uiState = UI_READY; // treat as backend speaking while ready
  }
}

void AppVoice::onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsReady = false;
      uiState = UI_WS_CONNECTING;
      sendHello();
      break;
    case WStype_DISCONNECTED:
      wsReady = false;
      streaming = false;
      startPending = false;
      wsStarted = false;
      uiState = (WiFi.status() == WL_CONNECTED) ? UI_WS_CONNECTING : UI_WIFI_CONNECTING;
      break;
    case WStype_TEXT: {
      String text = String(reinterpret_cast<char*>(payload), length);
      handleJson(text);
      break;
    }
    case WStype_BIN:
      handleBinary(payload, length);
      break;
    case WStype_ERROR:
      break;
    default:
      break;
  }
}

void AppVoice::setError(const char* msg) {
  strncpy(errorMsg, msg, sizeof(errorMsg) - 1);
  errorMsg[sizeof(errorMsg) - 1] = '\0';
  uiState = UI_ERROR;
}

void AppVoice::wsEventThunk(WStype_t type, uint8_t* payload, size_t length) {
  if (gAppVoice) gAppVoice->onWsEvent(type, payload, length);
}

void AppVoice::handleInput(InputService& input) {
  if (!wsReady) return;

  if (input.pressed(BTN_A) && !streaming) {
    streaming = true;
    startPending = true;
    uiState = UI_STREAMING;
    sendStart();
    micIn.setMode(MIC_BACKEND_STREAM);
    audioOut.playToneMidi(81, 60); // ~880 Hz start beep
  }

  if (input.released(BTN_A) && streaming) {
    streaming = false;
    micIn.setMode(MIC_OFF);
    memset(micPcm, 0, sizeof(micPcm));
    sendAudioFrame(false, true);
    sendStop();
    uiState = wsReady ? UI_READY : UI_WS_CONNECTING;
    audioOut.playToneMidi(76, 80); // ~660 Hz stop beep
  }
}

void AppVoice::tick(unsigned long) {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    wsStarted = false;
    wsReady = false;
    if (streaming) {
      streaming = false;
      startPending = false;
      micIn.setMode(MIC_OFF);
    }
    if (now - lastWifiAttemptMs > 4000) {
      startWifi();
    }
    uiState = UI_WIFI_CONNECTING;
    return;
  } else if (!wifiLoggedUp) {
    wifiLoggedUp = true;
  }

  if (!wsStarted) startWebSocket();
  ws.loop();

  if (!wsReady) return;

  if (streaming) {
    if (micIn.readPcm16(micPcm, FRAME_SAMPLES)) {
      bool startFlag = startPending;
      startPending = false;
      sendAudioFrame(startFlag, false);
    }
  } else {
    if (now - lastPingMs > PING_INTERVAL_MS) {
      lastPingMs = now;
      sendPing();
    }
  }
}

void AppVoice::render(DisplayService& display) {
  display.drawText(0, 0, "VOICE", 1);
  IPAddress ip = WiFi.localIP();
  char net[24];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(net, sizeof(net), "WiFi %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  } else {
    snprintf(net, sizeof(net), "WiFi not connected");
  }
  display.drawText(0, 12, net, 1);

  switch (uiState) {
    case UI_WIFI_CONNECTING:
      display.drawCentered("WIFI...", 24, 2);
      display.drawText(0, 56, "Connecting to WiFi", 1);
      break;
    case UI_WS_CONNECTING:
      display.drawCentered("BACKEND...", 24, 2);
      display.drawText(0, 56, "Waiting for backend", 1);
      break;
    case UI_READY:
      display.drawCentered("READY", 24, 2);
      display.drawText(0, 56, "Hold A to talk", 1);
      break;
    case UI_STREAMING:
      display.drawCentered("LISTENING", 24, 2);
      display.drawText(0, 56, "Release A to send", 1);
      break;
    case UI_ERROR:
      display.drawCentered("ERROR", 16, 2);
      display.drawText(0, 36, errorMsg, 1);
      break;
  }
}
