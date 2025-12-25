#include "AppRecorder.h"
#include "DisplayService.h"
#include "InputService.h"
#include "Pins.h"

AppRecorder::AppRecorder(MicInService& mic, AudioOutService& audio)
  : micIn(mic), audioOut(audio) {}

void AppRecorder::onEnter() {
  micIn.setMode(MIC_OFF);
  playing = false;
}

void AppRecorder::onExit() {
  micIn.setMode(MIC_OFF);
}

void AppRecorder::handleInput(InputService& input) {
  if (input.pressed(BTN_A)) {
    if (recording) stopRecording();
    else startRecording();
    audioOut.playSfx(SFX_START);
  }
  if (input.pressed(BTN_B)) {
    startPlayback();
    audioOut.playSfx(SFX_START);
  }
  if (input.pressed(BTN_SELECT)) {
    clearRecording();
    audioOut.playSfx(SFX_CLICK);
  }
}

void AppRecorder::tick(unsigned long) {
  if (playing) {
    int remaining = framesRecorded - playIndex;
    if (remaining <= 0) {
      playing = false;
      return;
    }
    int freeFrames = audioOut.pcmFree();
    if (freeFrames > 0) {
      int toQueue = remaining > freeFrames ? freeFrames : remaining;
      int queued = audioOut.playPcm(&buffer[playIndex], toQueue);
      playIndex += queued;
    }
  }

  if (!recording) return;
  int framesLeft = kMaxFrames - framesRecorded;
  if (framesLeft <= 0) {
    stopRecording();
    return;
  }

  int chunk = (framesLeft > AUDIO_FRAMES) ? AUDIO_FRAMES : framesLeft;
  if (micIn.readPcm16(&buffer[framesRecorded], chunk)) {
    framesRecorded += chunk;
  }
}

void AppRecorder::render(DisplayService& display) {
  display.drawText(0, 0, "RECORDER", 1);
  display.drawText(0, 16, recording ? "REC: ON" : "REC: OFF", 1);

  char info[24];
  int seconds = framesRecorded / AUDIO_SAMPLE_RATE;
  snprintf(info, sizeof(info), "LEN: %ds", seconds);
  display.drawText(0, 28, info, 1);

  float level = micIn.rmsLevel();
  int bar = (int)(level * 100.0f);
  if (bar > 100) bar = 100;
  display.drawRect(0, 44, 100, 8);
  display.fillRect(0, 44, bar, 8);

  display.drawText(0, 56, "A rec  B play  SEL clr", 1);
}

void AppRecorder::startRecording() {
  framesRecorded = 0;
  recording = true;
  playing = false;
  playIndex = 0;
  audioOut.stop();
  micIn.setMode(MIC_LOCAL_RECORD);
}

void AppRecorder::stopRecording() {
  recording = false;
  micIn.setMode(MIC_OFF);
}

void AppRecorder::startPlayback() {
  if (framesRecorded <= 0 || recording) return;
  playing = true;
  playIndex = 0;
  audioOut.stop();
}

void AppRecorder::clearRecording() {
  if (recording) return;
  framesRecorded = 0;
  playing = false;
  playIndex = 0;
  audioOut.stop();
}
