#include "MicInService.h"
#include "Pins.h"
#include "driver/i2s.h"
#include <math.h>

#define I2S_IN_PORT I2S_NUM_0

void MicInService::begin() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(
      I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB
    ),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = AUDIO_FRAMES,
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
  i2s_set_clk(I2S_IN_PORT, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void MicInService::tick(unsigned long) {
  if (currentMode == MIC_OFF) return;
}

void MicInService::setMode(MicMode mode) {
  currentMode = mode;
}

bool MicInService::readPcm16(int16_t* outBuf, int frames) {
  if (!outBuf || frames <= 0) return false;

  static int32_t in32[AUDIO_FRAMES];
  if (frames > AUDIO_FRAMES) frames = AUDIO_FRAMES;
  size_t nbytes = 0;

  if (i2s_read(I2S_IN_PORT, in32, frames * sizeof(int32_t), &nbytes, 0) != ESP_OK ||
      nbytes != (size_t)(frames * sizeof(int32_t))) {
    return false;
  }

  int64_t sumSq = 0;
  for (int i = 0; i < frames; ++i) {
    int16_t s = (int16_t)(in32[i] >> 14);
    outBuf[i] = s;
    sumSq += (int32_t)s * (int32_t)s;
  }

  if (frames > 0) {
    float meanSq = (float)sumSq / (float)frames;
    lastRms = sqrtf(meanSq) / 32768.0f;
  }
  return true;
}
