#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include "driver/i2s.h"

using namespace websockets;

// -------- WIFI --------
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";

// -------- WS --------
// Option A (wss): use wss://<domain>/voice and configure cert if needed.
// Option B (ws):  use ws://<ip>:<port>/voice for local testing.
const char* WS_URL = "wss://your-domain/voice";
const char* DEVICE_ID = "brick01";
const char* AUTH_TOKEN = "YOUR_TOKEN";

// -------- BUTTON --------
const int PIN_BTN_A = 39;

// -------- I2S OUT (MAX98357) --------
#define I2S_OUT_PORT I2S_NUM_1
#define PIN_I2S_OUT_DO   9
#define PIN_I2S_OUT_BCK  10
#define PIN_I2S_OUT_WS   11

// -------- I2S IN (MIC) --------
#define I2S_IN_PORT I2S_NUM_0
#define PIN_I2S_IN_SD    16
#define PIN_I2S_IN_BCK   17
#define PIN_I2S_IN_WS    18

// -------- AUDIO --------
#define SAMPLE_RATE 24000
#define FRAME_SAMPLES 480   // 20 ms @ 24 kHz
#define FRAME_BYTES (FRAME_SAMPLES * 2)

WebsocketsClient ws;
bool wsReady = false;
bool streaming = false;
bool startPending = false;
uint16_t txSeq = 0;

unsigned long lastPingMs = 0;
const unsigned long PING_INTERVAL_MS = 12000;

static int32_t micIn32[FRAME_SAMPLES];
static int16_t micPcm16[FRAME_SAMPLES];
static uint8_t txFrame[12 + FRAME_BYTES];

void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }
}

void i2s_out_init() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAME_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_I2S_OUT_BCK,
    .ws_io_num = PIN_I2S_OUT_WS,
    .data_out_num = PIN_I2S_OUT_DO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_OUT_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_OUT_PORT, &pins);
  i2s_set_clk(I2S_OUT_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void i2s_in_init() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(
      I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB
    ),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAME_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_I2S_IN_BCK,
    .ws_io_num = PIN_I2S_IN_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_IN_SD
  };

  i2s_driver_install(I2S_IN_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_IN_PORT, &pins);
  i2s_set_clk(I2S_IN_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void send_hello() {
  String msg = String("{\"type\":\"hello\",\"device_id\":\"") + DEVICE_ID +
               String("\",\"auth\":\"") + AUTH_TOKEN +
               String("\",\"sample_rate\":24000,\"channels\":1}");
  ws.send(msg);
}

void send_start() {
  ws.send("{\"type\":\"start\",\"mode\":\"voice\"}");
}

void send_stop() {
  ws.send("{\"type\":\"stop\"}");
}

void send_ping() {
  String msg = String("{\"type\":\"ping\",\"t\":") + String(millis()) + "}";
  ws.send(msg);
}

void send_audio_frame(bool startFlag, bool endFlag) {
  // Build header
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

  memcpy(txFrame + 12, micPcm16, FRAME_BYTES);
  ws.sendBinary(txFrame, sizeof(txFrame));
}

void handle_json(const String& text) {
  if (text.indexOf("\"type\":\"ready\"") >= 0) {
    wsReady = true;
  }
  Serial.println(text);
}

void handle_binary(const uint8_t* data, size_t len) {
  if (len < 12) return;
  uint16_t magic = data[0] | (data[1] << 8);
  uint8_t version = data[2];
  uint16_t samples = data[6] | (data[7] << 8);
  size_t expected = 12 + samples * 2;
  if (magic != 0xA0B1 || version != 1 || expected != len) return;
  const int16_t* pcm = reinterpret_cast<const int16_t*>(data + 12);

  static int16_t outStereo[FRAME_SAMPLES * 2];
  for (int i = 0; i < samples; ++i) {
    outStereo[2 * i] = pcm[i];
    outStereo[2 * i + 1] = pcm[i];
  }
  size_t bytesWritten = 0;
  i2s_write(I2S_OUT_PORT, outStereo, samples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
}

void ws_connect() {
  ws.onEvent([&](WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
      send_hello();
    } else if (event == WebsocketsEvent::ConnectionClosed) {
      wsReady = false;
    }
  });

  ws.onMessage([&](WebsocketsMessage message) {
    if (message.isText()) {
      handle_json(message.data());
    } else if (message.isBinary()) {
      auto bin = message.data();
      handle_binary(reinterpret_cast<const uint8_t*>(bin.c_str()), bin.length());
    }
  });

  // For WSS with ArduinoWebsockets, you may need setInsecure():
  // ws.setInsecure();
  ws.connect(WS_URL);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  wifi_connect();
  i2s_out_init();
  i2s_in_init();
  ws_connect();
}

void loop() {
  ws.poll();

  bool pressed = digitalRead(PIN_BTN_A) == LOW;
  if (pressed && !streaming && wsReady) {
    streaming = true;
    startPending = true;
    send_start();
  }
  if (!pressed && streaming) {
    streaming = false;
    bool endFlag = true;
    send_audio_frame(false, endFlag);
    send_stop();
  }

  if (streaming && wsReady) {
    size_t nbytes = 0;
    if (i2s_read(I2S_IN_PORT, micIn32, sizeof(micIn32), &nbytes, portMAX_DELAY) == ESP_OK &&
        nbytes == sizeof(micIn32)) {
      for (int i = 0; i < FRAME_SAMPLES; ++i) {
        micPcm16[i] = (int16_t)(micIn32[i] >> 14);
      }
      bool startFlag = false;
      if (startPending) {
        startFlag = true;
        startPending = false;
      }
      send_audio_frame(startFlag, false);
    }
  } else {
    unsigned long now = millis();
    if (now - lastPingMs > PING_INTERVAL_MS) {
      lastPingMs = now;
      send_ping();
    }
  }
}
