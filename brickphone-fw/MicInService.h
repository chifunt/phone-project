#pragma once

#include <Arduino.h>

enum MicMode {
  MIC_OFF = 0,
  MIC_SERIAL_STREAM,
  MIC_BACKEND_STREAM,
  MIC_LOCAL_RECORD
};

class MicInService {
public:
  void begin();
  void tick(unsigned long nowMs);
  void setMode(MicMode mode);
  MicMode mode() const { return currentMode; }

  bool readPcm16(int16_t* outBuf, int frames);
  float rmsLevel() const { return lastRms; }

private:
  MicMode currentMode = MIC_OFF;
  float lastRms = 0.0f;
};
