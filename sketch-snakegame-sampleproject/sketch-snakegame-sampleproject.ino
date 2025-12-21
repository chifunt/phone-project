#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include <math.h>

// ===================== GAME TYPES FIRST (avoids Arduino prototype issues) =====================
enum Dir { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

// Forward declarations (also avoids prototype issues)
void updateButtons();
void handleInput();
void stepGame();
void render();
bool isOpposite(Dir a, Dir b);

// ===================== BUTTONS (your pins) =====================
struct Btn {
  const char* name;
  int pin;
  bool last;
};

Btn buttons[] = {
  {"RIGHT", 35, true},
  {"UP", 36, true},
  {"DOWN", 37, true},
  {"LEFT", 38, true},
  {"A", 39, true},
  {"B", 40, true},
  {"SELECT", 41, true},
  {"START", 42, true},
};

const int BTN_N = sizeof(buttons) / sizeof(buttons[0]);
const unsigned long debounceMs = 25;
unsigned long lastChangeMs[BTN_N];

bool btnPressed[BTN_N];

enum BtnIdx { BI_RIGHT=0, BI_UP=1, BI_DOWN=2, BI_LEFT=3, BI_A=4, BI_B=5, BI_SELECT=6, BI_START=7 };

// ===================== OLED (your pins) =====================
#define SDA_PIN 4
#define SCL_PIN 5

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===================== AUDIO (your pins) =====================
#define PIN_I2S_DO   9
#define PIN_I2S_BCK  10
#define PIN_I2S_WS   11
#define I2S_PORT     I2S_NUM_1

#define SAMPLE_RATE  44100
#define BUF_FRAMES   256
#define MAX_AMP      30000

float MASTER_VOLUME = 0.35f;
bool soundEnabled = true;

// ===================== AUDIO SYNTH =====================
static inline float midiToHz(int midi) {
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

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

void playToneMidi(int midi, int ms) {
  if (!soundEnabled) return;
  if (midi < 0 || ms <= 0) return;

  const float freq = midiToHz(midi);
  const int totalSamples = (int)((ms / 1000.0f) * SAMPLE_RATE);

  const int attack = max(1, (int)(0.02f * totalSamples));
  const int release = max(1, (int)(0.10f * totalSamples));
  const float sustain = 0.75f;

  static int16_t buffer[BUF_FRAMES * 2];
  float phase = 0.0f;
  int idx = 0;

  while (idx < totalSamples) {
    const int chunk = min(BUF_FRAMES, totalSamples - idx);

    for (int i = 0; i < chunk; i++) {
      const int t = idx + i;
      float env = sustain;

      if (t < attack) env = (float)t / (float)attack;
      else if (t > totalSamples - release) env = sustain * (float)(totalSamples - t) / (float)release;

      phase += 2.0f * (float)M_PI * freq / (float)SAMPLE_RATE;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

      float s = sinf(phase) * env * MASTER_VOLUME;
      int16_t sample = (int16_t)(s * MAX_AMP);

      buffer[2*i]     = sample;
      buffer[2*i + 1] = sample;
    }

    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, buffer, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    idx += chunk;
  }
}

// SFX
void sfxStart()  { playToneMidi(72, 70); playToneMidi(76, 90); }
void sfxEat()    { playToneMidi(84, 50); playToneMidi(88, 60); }
void sfxOver()   { playToneMidi(60, 120); playToneMidi(55, 180); }
void sfxToggle() { playToneMidi(79, 40); }

// ===================== SNAKE GAME =====================
static const int CELL = 4;
static const int GRID_W = SCREEN_WIDTH / CELL;   // 32
static const int GRID_H = SCREEN_HEIGHT / CELL;  // 16
static const int MAX_CELLS = GRID_W * GRID_H;    // 512

struct Pt { uint8_t x; uint8_t y; };

Pt snake[MAX_CELLS];
int snakeLen = 0;

Pt food;
int score = 0;

Dir dir = DIR_RIGHT;
Dir nextDir = DIR_RIGHT;

bool running = false;
bool gameOver = false;

unsigned long lastStepMs = 0;
unsigned long stepIntervalMs = 120;
bool fastMode = false;

bool snakeContains(uint8_t x, uint8_t y) {
  for (int i = 0; i < snakeLen; i++) {
    if (snake[i].x == x && snake[i].y == y) return true;
  }
  return false;
}

void spawnFood() {
  for (int tries = 0; tries < 2000; tries++) {
    uint8_t x = (uint8_t)random(0, GRID_W);
    uint8_t y = (uint8_t)random(0, GRID_H);
    if (!snakeContains(x, y)) {
      food = {x, y};
      return;
    }
  }
  food = { (uint8_t)(GRID_W / 2), (uint8_t)(GRID_H / 2) };
}

void resetGame() {
  score = 0;
  snakeLen = 3;

  uint8_t sx = GRID_W / 2;
  uint8_t sy = GRID_H / 2;

  snake[0] = { (uint8_t)sx, (uint8_t)sy };
  snake[1] = { (uint8_t)(sx - 1), (uint8_t)sy };
  snake[2] = { (uint8_t)(sx - 2), (uint8_t)sy };

  dir = DIR_RIGHT;
  nextDir = DIR_RIGHT;

  spawnFood();

  running = false;
  gameOver = false;
  lastStepMs = millis();
}

bool isOpposite(Dir a, Dir b) {
  if (a == DIR_UP && b == DIR_DOWN) return true;
  if (a == DIR_DOWN && b == DIR_UP) return true;
  if (a == DIR_LEFT && b == DIR_RIGHT) return true;
  if (a == DIR_RIGHT && b == DIR_LEFT) return true;
  return false;
}

// ===================== INPUT =====================
void updateButtons() {
  unsigned long now = millis();
  for (int i = 0; i < BTN_N; i++) btnPressed[i] = false;

  for (int i = 0; i < BTN_N; i++) {
    bool cur = digitalRead(buttons[i].pin);

    if (cur != buttons[i].last && (now - lastChangeMs[i]) > debounceMs) {
      lastChangeMs[i] = now;
      buttons[i].last = cur;

      if (cur == LOW) btnPressed[i] = true;
    }
  }
}

void handleInput() {
  if (btnPressed[BI_UP])    nextDir = DIR_UP;
  if (btnPressed[BI_DOWN])  nextDir = DIR_DOWN;
  if (btnPressed[BI_LEFT])  nextDir = DIR_LEFT;
  if (btnPressed[BI_RIGHT]) nextDir = DIR_RIGHT;

  if (!isOpposite(dir, nextDir)) dir = nextDir;
  else nextDir = dir;

  if (btnPressed[BI_A]) {
    soundEnabled = !soundEnabled;
    sfxToggle();
  }

  if (btnPressed[BI_SELECT]) {
    fastMode = !fastMode;
    stepIntervalMs = fastMode ? 80 : 120;
    sfxToggle();
  }

  if (btnPressed[BI_B]) {
    resetGame();
    sfxStart();
  }

  if (btnPressed[BI_START]) {
    if (gameOver) resetGame();
    running = true;
    sfxStart();
  }
}

// ===================== GAME STEP =====================
void stepGame() {
  int nx = snake[0].x;
  int ny = snake[0].y;

  if (dir == DIR_UP) ny--;
  if (dir == DIR_DOWN) ny++;
  if (dir == DIR_LEFT) nx--;
  if (dir == DIR_RIGHT) nx++;

  if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
    running = false;
    gameOver = true;
    sfxOver();
    return;
  }

  if (snakeContains((uint8_t)nx, (uint8_t)ny)) {
    running = false;
    gameOver = true;
    sfxOver();
    return;
  }

  bool ate = ((uint8_t)nx == food.x && (uint8_t)ny == food.y);

  int newLen = snakeLen + (ate ? 1 : 0);
  for (int i = newLen - 1; i >= 1; i--) snake[i] = snake[i - 1];

  snake[0] = { (uint8_t)nx, (uint8_t)ny };
  snakeLen = newLen;

  if (ate) {
    score++;
    sfxEat();
    spawnFood();
  }
}

// ===================== RENDER =====================
void drawCenteredText(const char* text, int y, int size) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void render() {
  display.clearDisplay();

  if (!running && !gameOver) {
    drawCenteredText("SNAKE", 8, 2);
    display.setTextSize(1);
    display.setCursor(10, 32);
    display.print("START: play");
    display.setCursor(10, 42);
    display.print("B: reset  A: sound");
    display.setCursor(10, 52);
    display.print("SELECT: speed");
    display.display();
    return;
  }

  display.fillRect(food.x * CELL, food.y * CELL, CELL, CELL, SSD1306_WHITE);

  for (int i = 0; i < snakeLen; i++) {
    int px = snake[i].x * CELL;
    int py = snake[i].y * CELL;
    if (i == 0) display.fillRect(px, py, CELL, CELL, SSD1306_WHITE);
    else display.drawRect(px, py, CELL, CELL, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("S:");
  display.print(score);
  display.setCursor(40, 0);
  display.print(soundEnabled ? "snd" : "mut");
  display.setCursor(75, 0);
  display.print(fastMode ? "fast" : "norm");

  if (gameOver) {
    drawCenteredText("GAME OVER", 22, 1);
    drawCenteredText("START or B", 34, 1);
  }

  display.display();
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(150);

  for (int i = 0; i < BTN_N; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].last = digitalRead(buttons[i].pin);
    lastChangeMs[i] = 0;
    btnPressed[i] = false;
  }

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      while (true) delay(1000);
    }
  }
  display.clearDisplay();
  display.display();

  audio_init();

  randomSeed((uint32_t)micros());
  resetGame();
}

void loop() {
  updateButtons();
  handleInput();

  unsigned long now = millis();
  if (running && !gameOver) {
    if (now - lastStepMs >= stepIntervalMs) {
      lastStepMs = now;
      stepGame();
    }
  }

  render();
  delay(5);
}
