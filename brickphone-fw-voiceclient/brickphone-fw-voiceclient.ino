#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "driver/i2s.h"

// -------- WIFI --------
#include "secrets.h"

#ifndef WIFI_SSID_STR
#define WIFI_SSID_STR "YOUR_SSID"
#endif

#ifndef WIFI_PASS_STR
#define WIFI_PASS_STR "YOUR_PASS"
#endif

const char* WIFI_SSID = WIFI_SSID_STR;
const char* WIFI_PASS = WIFI_PASS_STR;

// -------- WS --------
#define SKIP_I2S_FOR_DEBUG 0

const char* WS_HOST = "phone-project.joshuatjhie.workers.dev";
const char* WS_PATH = "/voice";
const char* DEVICE_ID = "brick01";
#ifndef BRICKPHONE_TOKEN
#define BRICKPHONE_TOKEN "YOUR_TOKEN_HERE"
#endif

const char* AUTH_TOKEN = BRICKPHONE_TOKEN;

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

WebSocketsClient ws;
bool wsReady = false;
bool streaming = false;
bool startPending = false;
uint16_t txSeq = 0;

unsigned long lastPingMs = 0;
const unsigned long PING_INTERVAL_MS = 12000;

static int32_t micIn32[FRAME_SAMPLES];
static int16_t micPcm16[FRAME_SAMPLES];
static uint8_t txFrame[12 + FRAME_BYTES];
static int16_t outStereo[FRAME_SAMPLES * 2];
static int16_t beepBuf[FRAME_SAMPLES * 2];

void wifi_connect() {
  Serial.println("WiFi: connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi: connected ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.RSSI());
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

void play_beep(int freq, int ms) {
  int totalSamples = (ms * SAMPLE_RATE) / 1000;
  int offset = 0;
  float phase = 0.0f;
  float step = 2.0f * 3.14159265f * (float)freq / (float)SAMPLE_RATE;
  while (offset < totalSamples) {
    int chunk = FRAME_SAMPLES;
    if (totalSamples - offset < chunk) chunk = totalSamples - offset;
    for (int i = 0; i < chunk; ++i) {
      int16_t s = (int16_t)(sinf(phase) * 3000.0f);
      phase += step;
      if (phase > 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
      beepBuf[2 * i] = s;
      beepBuf[2 * i + 1] = s;
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_OUT_PORT, beepBuf, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    offset += chunk;
  }
}

void send_hello() {
  String msg = String("{\"type\":\"hello\",\"device_id\":\"") + DEVICE_ID +
               String("\",\"auth\":\"") + AUTH_TOKEN +
               String("\",\"sample_rate\":24000,\"channels\":1}");
  ws.sendTXT(msg);
  Serial.println("WS: hello sent");
}

void send_start() {
  ws.sendTXT("{\"type\":\"start\",\"mode\":\"voice\"}");
  Serial.println("WS: start sent");
}

void send_stop() {
  ws.sendTXT("{\"type\":\"stop\"}");
  Serial.println("WS: stop sent");
}

void send_ping() {
  String msg = String("{\"type\":\"ping\",\"t\":") + String(millis()) + "}";
  ws.sendTXT(msg);
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
  ws.sendBIN(txFrame, sizeof(txFrame));
}

void handle_json(const String& text) {
  if (text.indexOf("\"type\":\"ready\"") >= 0) {
    wsReady = true;
    Serial.println("WS: ready");
  }
  Serial.print("WS JSON: ");
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

  for (int i = 0; i < samples; ++i) {
    outStereo[2 * i] = pcm[i];
    outStereo[2 * i + 1] = pcm[i];
  }
  size_t bytesWritten = 0;
  i2s_write(I2S_OUT_PORT, outStereo, samples * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("WS: ConnectionOpened");
      wsReady = true;
      send_hello();
      break;
    case WStype_DISCONNECTED:
      Serial.print("WS: ConnectionClosed");
      if (length > 0) {
        Serial.print(" payload(");
        Serial.print(length);
        Serial.print("): ");
        size_t maxLen = length > 60 ? 60 : length;
        for (size_t i = 0; i < maxLen; ++i) {
          Serial.print((char)payload[i]);
        }
      }
      Serial.println();
      wsReady = false;
      break;
    case WStype_TEXT: {
      String text = String(reinterpret_cast<char*>(payload), length);
      handle_json(text);
      break;
    }
    case WStype_BIN:
      Serial.println("WS BIN");
      handle_binary(payload, length);
      break;
    case WStype_PING:
      Serial.println("WS: GotPing");
      break;
    case WStype_PONG:
      Serial.println("WS: GotPong");
      break;
    default:
      break;
  }
}

void ws_connect() {
  Serial.print("WS: wss://");
  Serial.print(WS_HOST);
  Serial.println(WS_PATH);
  Serial.println("WS: attempting connect");
  ws.onEvent(webSocketEvent);
  ws.setReconnectInterval(2000);
  ws.beginSSL(WS_HOST, 443, WS_PATH);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Voice client boot");
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  wifi_connect();
  WiFi.setSleep(false);
  Serial.print("heap before ws: ");
  Serial.println(ESP.getFreeHeap());
  ws_connect();
  Serial.print("heap after ws: ");
  Serial.println(ESP.getFreeHeap());
#if !SKIP_I2S_FOR_DEBUG
  i2s_out_init();
  i2s_in_init();
#endif
}

void loop() {
  ws.loop();

#if SKIP_I2S_FOR_DEBUG
  unsigned long now = millis();
  if (now - lastPingMs > PING_INTERVAL_MS) {
    lastPingMs = now;
    send_ping();
  }
  return;
#endif

  bool pressed = digitalRead(PIN_BTN_A) == LOW;
  if (pressed && !streaming && wsReady) {
    Serial.println("PTT: start");
    streaming = true;
    startPending = true;
    send_start();
#if !SKIP_I2S_FOR_DEBUG
    play_beep(880, 60);
#endif
  }
  if (!pressed && streaming) {
    Serial.println("PTT: stop");
    streaming = false;
    bool endFlag = true;
    send_audio_frame(false, endFlag);
    send_stop();
#if !SKIP_I2S_FOR_DEBUG
    play_beep(660, 80);
#endif
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
