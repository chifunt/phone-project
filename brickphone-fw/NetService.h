#pragma once

#include <Arduino.h>

class NetService {
public:
  void begin();
  void tick(unsigned long nowMs);
  void connectWifi(const char* ssid, const char* pass);
  bool isConnected() const;

  void sendAudioFrame(const int16_t* pcm, int frames);
  void sendEvent(const char* type, const char* payload);

private:
  bool connecting = false;
  unsigned long lastCheckMs = 0;
};
