#include <Arduino.h>
#include "driver/i2s.h"

#define I2S_BCLK  17   // SCK / BCLK
#define I2S_LRCL  18   // WS / LRCL
#define I2S_DOUT  16   // SD  -> ESP32 data-in
#define I2S_PORT  I2S_NUM_0
#define SR        16000

void setup_i2s() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SR,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,       // mic: 24 in 32
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,        // L/R pin tied to GND
    .communication_format = (i2s_comm_format_t)(
      I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB     // try both edges
    ),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRCL,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_DOUT
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SR, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void setup() {
  Serial.begin(115200);  // CDC ignores the number; keep it conventional
  setup_i2s();
}

void loop() {
  const int N = 256;
  int32_t in32[N];
  size_t nbytes = 0;

  if (i2s_read(I2S_PORT, in32, sizeof(in32), &nbytes, portMAX_DELAY) == ESP_OK &&
      nbytes == sizeof(in32)) {
    // Convert 24-bit MSB-in-32 to 16-bit PCM
    int16_t out16[N];
    for (int i = 0; i < N; ++i) out16[i] = (int16_t)(in32[i] >> 14); // try 13 if quiet
    Serial.write(reinterpret_cast<uint8_t*>(out16), sizeof(out16));   // PURE BINARY — no prints
  }
}
