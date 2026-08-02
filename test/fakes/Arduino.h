#pragma once
// Минимальная подмена Arduino.h для native-тестов.
// Пишет историю обращений к пинам, чтобы тест мог проверить не только
// итоговое состояние, но и весь путь к нему: щелчок реле при загрузке это
// именно промежуточная запись, а не конечная.
#include <stdint.h>
#include <vector>

#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1

struct PinWrite {
  int pin;
  int level;
};

inline std::vector<PinWrite>& pinWrites() {
  static std::vector<PinWrite> writes;
  return writes;
}

inline std::vector<int>& pinModeCalls() {
  static std::vector<int> calls;
  return calls;
}

inline unsigned long& fakeMillis() {
  static unsigned long now = 0;
  return now;
}

inline void resetArduinoFake() {
  pinWrites().clear();
  pinModeCalls().clear();
  fakeMillis() = 0;
}

inline void pinMode(int pin, int mode) {
  (void)mode;
  pinModeCalls().push_back(pin);
}

inline void digitalWrite(int pin, int level) {
  pinWrites().push_back({pin, level});
}

inline unsigned long millis() {
  return fakeMillis();
}
