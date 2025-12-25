#include "NetService.h"
#include <WiFi.h>

void NetService::begin() {
  WiFi.mode(WIFI_STA);
}

void NetService::tick(unsigned long nowMs) {
  if (!connecting) return;
  if (nowMs - lastCheckMs < 500) return;
  lastCheckMs = nowMs;
  if (WiFi.status() == WL_CONNECTED) {
    connecting = false;
  }
}

void NetService::connectWifi(const char* ssid, const char* pass) {
  if (!ssid || ssid[0] == '\0') return;
  WiFi.begin(ssid, pass);
  connecting = true;
  lastCheckMs = 0;
}

bool NetService::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void NetService::sendAudioFrame(const int16_t*, int) {
}

void NetService::sendEvent(const char*, const char*) {
}
