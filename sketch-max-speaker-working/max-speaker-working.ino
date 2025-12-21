#include "driver/i2s.h"
#include <math.h>
#include <Arduino.h>

// ---------------- I2S PINS (ESP32-S3) ----------------
#define PIN_I2S_DO   9    // MAX98357 DIN
#define PIN_I2S_BCK  10   // MAX98357 BCLK
#define PIN_I2S_WS   11   // MAX98357 LRC / WS
#define I2S_PORT     I2S_NUM_1
// -----------------------------------------------------

// ---------------- AUDIO SETTINGS ---------------------
#define SAMPLE_RATE  44100
#define BUF_FRAMES   512
#define MAX_AMP      30000     // never exceed ~32000 for 16-bit
// -----------------------------------------------------

// ---------------- MASTER VOLUME ----------------------
// 0.0 = silent
// 0.3 = quiet
// 0.6 = normal
// 1.0 = loud (careful)
float MASTER_VOLUME = 0.3f;
// -----------------------------------------------------

// ---------------- MIDI HELPERS -----------------------
static inline float midiToHz(int midi) {
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

struct Note {
  int midi;        // -1 = rest
  float beats;     // length in beats
};
// -----------------------------------------------------

// ---------------- KOROBEINIKI (TETRIS) ----------------
Note MELODY[] = {
  {76,0.5},{71,0.5},{72,0.5},{74,0.5},{72,0.5},{71,0.5},{69,0.5},{69,0.5},
  {72,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{72,0.5},{74,0.5},{76,0.5},
  {72,0.5},{69,0.5},{69,0.5},{-1,0.5},

  {74,0.5},{77,0.5},{81,0.5},{79,0.5},{77,0.5},{76,0.5},{72,0.5},{72,0.5},
  {74,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{71,0.5},{72,0.5},{74,0.5},
  {76,0.5},{72,0.5},{69,0.5},{-1,0.5}
};
const int MELODY_LEN = sizeof(MELODY) / sizeof(MELODY[0]);
// -----------------------------------------------------

// ---------------- TEMPO -------------------------------
float BPM = 80.0f;
static inline float beatsToSeconds(float beats) {
  return (60.0f / BPM) * beats;
}
// -----------------------------------------------------

// ---------------- I2S INIT ----------------------------
void audio_init() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = BUF_FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_I2S_BCK,
    .ws_io_num = PIN_I2S_WS,
    .data_out_num = PIN_I2S_DO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}
// -----------------------------------------------------

// ---------------- PLAY NOTE ---------------------------
void play_note(int midi, float seconds) {
  const float vibratoHz = 5.0f;
  const float vibratoDepth = 0.01f;

  int totalSamples = seconds * SAMPLE_RATE;
  int attack = totalSamples * 0.03f;
  int decay  = totalSamples * 0.07f;
  int release = totalSamples * 0.12f;
  float sustain = 0.7f;

  static int16_t buffer[BUF_FRAMES * 2];
  float phase = 0.0f;
  float baseFreq = (midi >= 0) ? midiToHz(midi) : 0.0f;

  int index = 0;
  while (index < totalSamples) {
    int chunk = min(BUF_FRAMES, totalSamples - index);

    for (int i = 0; i < chunk; i++) {
      int t = index + i;
      float env;

      if (t < attack) env = (float)t / attack;
      else if (t < attack + decay)
        env = 1.0f - (1.0f - sustain) * (float)(t - attack) / decay;
      else if (t > totalSamples - release)
        env = sustain * (float)(totalSamples - t) / release;
      else env = sustain;

      int16_t sample = 0;
      if (midi >= 0) {
        float f = baseFreq * (1.0f + vibratoDepth *
                  sinf(2.0f * M_PI * vibratoHz * t / SAMPLE_RATE));
        phase += 2.0f * M_PI * f / SAMPLE_RATE;
        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

        float s = sinf(phase);
        float scaled = s * env * MASTER_VOLUME;
        sample = (int16_t)(scaled * MAX_AMP);
      }

      buffer[2*i]     = sample;
      buffer[2*i + 1] = sample;
    }

    size_t bytes;
    i2s_write(I2S_PORT, buffer, chunk * 2 * sizeof(int16_t), &bytes, portMAX_DELAY);
    index += chunk;
  }
}
// -----------------------------------------------------

void setup() {
  audio_init();
  // MAX98357 SD must be tied to 3V3
}

void loop() {
  for (int i = 0; i < MELODY_LEN; i++) {
    play_note(MELODY[i].midi, beatsToSeconds(MELODY[i].beats));
  }
  vTaskDelay(700 / portTICK_PERIOD_MS);
}
