#include <Arduino.h>
#include "Pins.h"
#include "InputService.h"
#include "DisplayService.h"
#include "AudioOutService.h"
#include "MicInService.h"
#include "NetService.h"
#include "StorageService.h"
#include "ScreenManager.h"
#include "SplashScreen.h"
#include "MenuScreen.h"
#include "AppSnake.h"
#include "AppRecorder.h"
#include "AppVoice.h"
#include "AppSettings.h"
#include "AppPong.h"
#include "AppBreakout.h"
#include "AppSpaceInvaders.h"
#include "App2048.h"
#include "AppFlappy.h"

InputService input;
DisplayService display;
AudioOutService audioOut;
MicInService micIn;
NetService net;
StorageService storage;

ScreenManager screens;

SplashScreen splashScreen(audioOut, screens);
MenuScreen menuScreen(screens, audioOut);
AppSnake appSnake(audioOut);
AppRecorder appRecorder(micIn, audioOut);
AppVoice appVoice(micIn, audioOut);
AppSettings appSettings(audioOut, net, screens);
AppPong appPong(audioOut);
AppBreakout appBreakout(audioOut);
AppSpaceInvaders appSpaceInvaders(audioOut);
App2048 app2048(audioOut);
AppFlappy appFlappy(audioOut);

unsigned long lastTickMs = 0;
unsigned long lastDisplayMs = 0;
static const unsigned long kFrameMs = 33;

void setup() {
  Serial.begin(115200);
  delay(200);
  input.begin();
  display.begin();
  audioOut.begin();
  micIn.begin();
  net.begin();
  storage.begin();

  screens.registerScreen(ScreenId::Splash, &splashScreen);
  screens.registerScreen(ScreenId::Menu, &menuScreen);
  screens.registerScreen(ScreenId::Snake, &appSnake);
  screens.registerScreen(ScreenId::Recorder, &appRecorder);
  screens.registerScreen(ScreenId::Voice, &appVoice);
  screens.registerScreen(ScreenId::Settings, &appSettings);
  screens.registerScreen(ScreenId::Pong, &appPong);
  screens.registerScreen(ScreenId::Breakout, &appBreakout);
  screens.registerScreen(ScreenId::SpaceInvaders, &appSpaceInvaders);
  screens.registerScreen(ScreenId::Game2048, &app2048);
  screens.registerScreen(ScreenId::Flappy, &appFlappy);
  screens.setAudio(&audioOut);

  screens.set(ScreenId::Splash);
  lastTickMs = millis();
}

void loop() {
  unsigned long now = millis();
  unsigned long dt = now - lastTickMs;
  lastTickMs = now;

  input.poll(now);
  net.tick(now);
  micIn.tick(now);
  audioOut.tick(now);

  screens.tick(dt, input);

  if (now - lastDisplayMs >= kFrameMs) {
    lastDisplayMs = now;
    display.beginFrame();
    screens.render(display);
    display.endFrame();
  }
}
