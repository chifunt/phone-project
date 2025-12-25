#include "AudioOutService.h"
#include "Pins.h"
#include "driver/i2s.h"
#include <math.h>

#define I2S_OUT_PORT I2S_NUM_1
#define MAX_AMP      16000

static inline float midiToHz(int midi) {
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

void AudioOutService::begin() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = AUDIO_FRAMES,
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
  i2s_set_clk(I2S_OUT_PORT, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void AudioOutService::tick(unsigned long) {
  if (pcmData && pcmFramesLeft > 0) {
    int frames = (pcmFramesLeft > AUDIO_FRAMES) ? AUDIO_FRAMES : pcmFramesLeft;
    static int16_t buffer[AUDIO_FRAMES * 2];
    for (int i = 0; i < frames; ++i) {
      int16_t s = pcmData[i];
      buffer[2 * i] = s;
      buffer[2 * i + 1] = s;
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_OUT_PORT, buffer, frames * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    pcmData += frames;
    pcmFramesLeft -= frames;
    if (pcmFramesLeft <= 0) {
      pcmData = nullptr;
    }
    return;
  }

  if (!playing) return;
  renderFrames(AUDIO_FRAMES);
}

void AudioOutService::setVolume(float vol) {
  if (vol < 0.0f) vol = 0.0f;
  if (vol > 1.0f) vol = 1.0f;
  volume = vol;
}

void AudioOutService::playToneMidi(int midi, int ms) {
  Note single = { midi, ms };
  startSequence(&single, 1);
}

void AudioOutService::playSfx(SfxId id) {
  static const Note boot[]  = {
    {76, 120}, {74, 120}, {77, 120}, {79, 120},
    {73, 160}, {71, 160}, {76, 200}
  };
  static const Note click[] = { {79, 40} };
  static const Note start[] = { {72, 50}, {79, 50}, {84, 60} };
  static const Note eat[]   = { {84, 50}, {88, 60} };
  static const Note over[]  = { {60, 120}, {55, 180} };

  switch (id) {
    case SFX_BOOT:  startSequence(boot, sizeof(boot) / sizeof(boot[0])); break;
    case SFX_CLICK: startSequence(click, sizeof(click) / sizeof(click[0])); break;
    case SFX_START: startSequence(start, sizeof(start) / sizeof(start[0])); break;
    case SFX_EAT:   startSequence(eat, sizeof(eat) / sizeof(eat[0])); break;
    case SFX_OVER:  startSequence(over, sizeof(over) / sizeof(over[0])); break;
  }
}

void AudioOutService::playPcm(const int16_t* pcm, int frames) {
  if (!pcm || frames <= 0) return;
  pcmData = pcm;
  pcmFramesLeft = frames;
}

void AudioOutService::stop() {
  playing = false;
  sequence = nullptr;
  sequenceLen = 0;
  sequenceIndex = 0;
  currentMidi = -1;
  noteSamplesLeft = 0;
  noteTotalSamples = 0;
  pcmData = nullptr;
  pcmFramesLeft = 0;
}

void AudioOutService::startSequence(const Note* seq, uint8_t len) {
  sequence = seq;
  sequenceLen = len;
  sequenceIndex = 0;
  playing = true;
  advanceNote();
}

bool AudioOutService::advanceNote() {
  if (!sequence || sequenceIndex >= sequenceLen) {
    stop();
    return false;
  }
  currentMidi = sequence[sequenceIndex].midi;
  int ms = sequence[sequenceIndex].ms;
  noteTotalSamples = (int)((ms / 1000.0f) * AUDIO_SAMPLE_RATE);
  if (noteTotalSamples < 1) noteTotalSamples = 1;
  noteSamplesLeft = noteTotalSamples;
  sequenceIndex++;
  return true;
}

void AudioOutService::renderFrames(int frames) {
  static int16_t buffer[AUDIO_FRAMES * 2];

  for (int i = 0; i < frames; ++i) {
    int16_t sample = 0;
    if (currentMidi >= 0 && noteSamplesLeft > 0) {
      float freq = midiToHz(currentMidi);
      int sampleIndex = noteTotalSamples - noteSamplesLeft;
      float env = 1.0f;
      int attack = noteTotalSamples / 12;
      int release = noteTotalSamples / 8;
      if (attack < 1) attack = 1;
      if (release < 1) release = 1;
      if (sampleIndex < attack) env = (float)sampleIndex / (float)attack;
      else if (noteSamplesLeft < release) env = (float)noteSamplesLeft / (float)release;

      phase += 2.0f * (float)M_PI * freq / (float)AUDIO_SAMPLE_RATE;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

      float s = sinf(phase) * env * volume;
      sample = (int16_t)(s * MAX_AMP);
      noteSamplesLeft--;
    }

    buffer[2 * i] = sample;
    buffer[2 * i + 1] = sample;

    if (noteSamplesLeft <= 0 && playing) {
      advanceNote();
    }
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_OUT_PORT, buffer, frames * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
}
