#include <Arduino.h>
#include "driver/i2s.h"

// ---------------- AUDIO STANDARD ----------------
#define SR        24000
#define FRAMES    256

// ---------------- I2S MIC (RX) ------------------
#define I2S_IN_BCLK  17   // SCK / BCLK
#define I2S_IN_LRCL  18   // WS / LRCL
#define I2S_IN_DATA  16   // SD  -> ESP32 data-in
#define I2S_IN_PORT  I2S_NUM_0

// ---------------- I2S AMP (TX) ------------------
#define I2S_OUT_DATA  9    // MAX98357 DIN
#define I2S_OUT_BCLK  10   // MAX98357 BCLK
#define I2S_OUT_LRCL  11   // MAX98357 LRC / WS
#define I2S_OUT_PORT  I2S_NUM_1

static void setup_i2s_in() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SR,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // mic: 24 in 32
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // L/R pin tied to GND
    .communication_format = (i2s_comm_format_t)(
      I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB
    ),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_IN_BCLK,
    .ws_io_num = I2S_IN_LRCL,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_IN_DATA
  };

  i2s_driver_install(I2S_IN_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_IN_PORT, &pins);
  i2s_set_clk(I2S_IN_PORT, SR, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

static void setup_i2s_out() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SR,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_OUT_BCLK,
    .ws_io_num = I2S_OUT_LRCL,
    .data_out_num = I2S_OUT_DATA,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_OUT_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_OUT_PORT, &pins);
  i2s_set_clk(I2S_OUT_PORT, SR, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void setup() {
  setup_i2s_in();
  setup_i2s_out();
}

void loop() {
  int32_t in32[FRAMES];
  int16_t out16[FRAMES * 2];
  size_t nbytes = 0;

  if (i2s_read(I2S_IN_PORT, in32, sizeof(in32), &nbytes, portMAX_DELAY) == ESP_OK &&
      nbytes == sizeof(in32)) {
    for (int i = 0; i < FRAMES; ++i) {
      int16_t s = (int16_t)(in32[i] >> 14);  // 24-bit MSB-in-32 -> 16-bit
      out16[2 * i] = s;       // L
      out16[2 * i + 1] = s;   // R
    }

    size_t written = 0;
    i2s_write(I2S_OUT_PORT, out16, sizeof(out16), &written, portMAX_DELAY);
  }
}
