#include "AppSettings.h"
#include "DisplayService.h"
#include "InputService.h"

static const AppSettings::WifiPreset kWifiPresets[] = {
  { "Home WiFi", "YOUR_HOME_SSID", "YOUR_HOME_PASS" },
  { "Hotspot", "YOUR_HOTSPOT_SSID", "YOUR_HOTSPOT_PASS" }
};

static const int kWifiCount = sizeof(kWifiPresets) / sizeof(kWifiPresets[0]);

AppSettings::AppSettings(AudioOutService& audio, NetService& net, ScreenManager& screens)
  : audioOut(audio), netService(net), screenManager(screens) {}

void AppSettings::onEnter() {
  audioOut.setVolume(volumePercent / 100.0f);
}

void AppSettings::handleInput(InputService& input) {
  if (input.pressed(BTN_B)) {
    screenManager.set(ScreenId::Menu);
    return;
  }

  if (input.pressed(BTN_UP)) {
    volumePercent += 5;
    if (volumePercent > 100) volumePercent = 100;
    if (!muted) audioOut.setVolume(volumePercent / 100.0f);
  } else if (input.pressed(BTN_DOWN)) {
    volumePercent -= 5;
    if (volumePercent < 0) volumePercent = 0;
    if (!muted) audioOut.setVolume(volumePercent / 100.0f);
  }

  if (input.pressed(BTN_LEFT)) {
    wifiIndex = (wifiIndex == 0) ? (kWifiCount - 1) : (wifiIndex - 1);
  } else if (input.pressed(BTN_RIGHT)) {
    wifiIndex = (wifiIndex + 1) % kWifiCount;
  }

  if (input.pressed(BTN_A)) {
    if (muted) {
      muted = false;
      audioOut.setVolume(volumePercent / 100.0f);
    } else {
      muted = true;
      audioOut.setVolume(0.0f);
    }
  }

  if (input.pressed(BTN_SELECT)) {
    netService.connectWifi(kWifiPresets[wifiIndex].ssid, kWifiPresets[wifiIndex].pass);
  }
}

void AppSettings::render(DisplayService& display) {
  display.drawText(0, 0, "SETTINGS", 1);

  char vol[16];
  snprintf(vol, sizeof(vol), "VOL: %d", volumePercent);
  display.drawText(0, 14, vol, 1);
  display.drawRect(0, 24, 100, 6);
  int bar = volumePercent;
  display.fillRect(0, 24, bar, 6);
  if (muted) display.drawText(106, 22, "M", 1);

  display.drawText(0, 34, "WIFI:", 1);
  display.drawText(30, 34, kWifiPresets[wifiIndex].name, 1);

  display.drawText(0, 46, netService.isConnected() ? "STATUS: ON" : "STATUS: OFF", 1);
  display.drawText(0, 56, "SEL connect  B back", 1);
}
