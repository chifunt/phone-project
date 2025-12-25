#include <Arduino.h>

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

const int N = sizeof(buttons) / sizeof(buttons[0]);
const unsigned long debounceMs = 25;
unsigned long lastChangeMs[N];

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < N; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].last = digitalRead(buttons[i].pin);
    lastChangeMs[i] = 0;
  }

  Serial.println("Buttons ready. Press a button.");
}

void loop() {
  unsigned long now = millis();

  for (int i = 0; i < N; i++) {
    bool cur = digitalRead(buttons[i].pin);

    if (cur != buttons[i].last && (now - lastChangeMs[i]) > debounceMs) {
      lastChangeMs[i] = now;
      buttons[i].last = cur;

      if (cur == LOW) {
        Serial.print("Pressed: ");
      } else {
        Serial.print("Released: ");
      }
      Serial.println(buttons[i].name);
    }
  }
}
