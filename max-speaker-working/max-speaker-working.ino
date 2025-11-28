#include "driver/i2s.h"
#include <math.h>
#include <Arduino.h>

// ---- Your working pins (ESP32-S3) ----
#define PIN_I2S_DO   9    // MAX98357 DIN
#define PIN_I2S_BCK  10   // MAX98357 BCLK
#define PIN_I2S_WS   11   // MAX98357 LRC/WS
#define I2S_PORT     I2S_NUM_1
// --------------------------------------

#define SR           44100            // sample rate
#define AMP          28000            // output amplitude (16-bit)
#define BUF_FRAMES   512              // frames per I2S write

// Tie MAX98357 SD to 3V3 (physically). Speaker on SPK+ / SPK−.

// ---- MIDI helpers ----
static inline float midiToHz(int midi) {
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

// A small “note” struct: MIDI pitch (or 0 for rest) + duration in beats
struct Note { int midi; float beats; };

// Tetris / Korobeiniki — main A section (public domain folk tune).
// Key: A minor-ish. Tempo set below.
// You can tweak tempo or transpose by shifting MIDI numbers (+/- semitones).
Note MELODY[] = {
  // Phrase 1
  {76,0.5},{71,0.5},{72,0.5},{74,0.5},{72,0.5},{71,0.5},{69,0.5},{69,0.5},
  {72,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{72,0.5},{74,0.5},{76,0.5},
  {72,0.5},{69,0.5},{69,0.5},{-1,0.5},

  // Phrase 2
  {74,0.5},{77,0.5},{81,0.5},{79,0.5},{77,0.5},{76,0.5},{72,0.5},{72,0.5},
  {74,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{71,0.5},{72,0.5},{74,0.5},
  {76,0.5},{72,0.5},{69,0.5},{-1,0.5},

  // Repeat phrase 1
  {76,0.5},{71,0.5},{72,0.5},{74,0.5},{72,0.5},{71,0.5},{69,0.5},{69,0.5},
  {72,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{72,0.5},{74,0.5},{76,0.5},
  {72,0.5},{69,0.5},{69,0.5},{-1,0.5},

  // Tag
  {74,0.5},{77,0.5},{81,0.5},{79,0.5},{77,0.5},{76,0.5},{72,0.5},{72,0.5},
  {74,0.5},{76,0.5},{74,0.5},{72,0.5},{71,0.5},{71,0.5},{72,0.5},{74,0.5},
  {76,1.0},{-1,0.5}
};
const int MELODY_LEN = sizeof(MELODY)/sizeof(MELODY[0]);

// ---- I2S setup ----
void audio_init() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SR,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,    // stereo frames
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
  i2s_set_clk(I2S_PORT, SR, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

// ---- audio write helper ----
static inline void write_frames(int16_t *stereoLR, size_t frames) {
  size_t bytes;
  i2s_write(I2S_PORT, (const char*)stereoLR, frames * 2 * sizeof(int16_t), &bytes, portMAX_DELAY);
}

// Simple ADSR envelope generator (per note), with slight vibrato for sweetness
void play_note(int midi, float seconds) {
  const float vibHz = 5.0f;
  const float vibDepth = 0.01f;   // ±1% pitch
  const int total = (int)(seconds * SR);
  const int A = (int)(0.03f * total);
  const int D = (int)(0.07f * total);
  const float S = 0.7f;
  const int R = (int)(0.12f * total);

  static int16_t buf[BUF_FRAMES * 2];
  float phase = 0.0f;

  float baseFreq = (midi > 0) ? midiToHz(midi) : 0.0f;
  int idx = 0;
  while (idx < total) {
    int chunk = min(BUF_FRAMES, total - idx);
    for (int i = 0; i < chunk; i++) {
      int t = idx + i;
      // Envelope
      float env;
      if (t < A) env = (A ? (float)t / A : 1.0f);
      else if (t < A + D) env = 1.0f - (1.0f - S) * (float)(t - A) / (float)D;
      else if (t > total - R) env = S * max(0.0f, (float)(total - t) / (float)R);
      else env = S;

      int16_t sample = 0;
      if (midi > 0) {
        float f = baseFreq * (1.0f + vibDepth * sinf(2.0f * M_PI * vibHz * (float)t / SR));
        phase += 2.0f * M_PI * f / SR;
        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        float s = sinf(phase) * env;
        sample = (int16_t)(s * AMP);
      }
      buf[2*i]     = sample; // L
      buf[2*i + 1] = sample; // R
    }
    write_frames(buf, chunk);
    idx += chunk;
  }
}

// ---- tempo & playback ----
float BPM = 80.0f;              // adjust tempo here
float beatToSeconds(float beats){ return (60.0f / BPM) * beats; }

void setup() {
  audio_init();
  // Ensure MAX98357 SD is tied to 3V3 or left floating (not to GND).
}

void loop() {
  // Play the melody
  for (int i = 0; i < MELODY_LEN; i++) {
    int midi = MELODY[i].midi;
    float secs = beatToSeconds(MELODY[i].beats);
    play_note(midi, secs);
  }
  // Pause before repeating
  vTaskDelay(700 / portTICK_PERIOD_MS);
}
