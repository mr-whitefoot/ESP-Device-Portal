#pragma once
// Минимальная подмена Arduino.h для native-тестов.
// Пишет историю обращений к пинам, чтобы тест мог проверить не только
// итоговое состояние, но и весь путь к нему: щелчок реле при загрузке это
// именно промежуточная запись, а не конечная.
#include <stdint.h>

#include <map>
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

// Защёлка выхода. После сброса контроллера нулевая, поэтому карта отдаёт
// LOW для незнакомого пина -- как на живом железе.
inline std::map<int, int>& pinLatch() {
  static std::map<int, int> latch;
  return latch;
}

inline std::map<int, bool>& pinIsOutput() {
  static std::map<int, bool> outputs;
  return outputs;
}

inline void resetArduinoFake() {
  pinWrites().clear();
  pinModeCalls().clear();
  pinLatch().clear();
  pinIsOutput().clear();
  fakeMillis() = 0;
}

// Перевод пина в выход выкладывает на ножку содержимое защёлки -- именно
// поэтому порядок digitalWrite и pinMode имеет значение, и именно это
// пропускал прежний фейк, где pinMode вообще не влиял на уровень.
inline void pinMode(int pin, int mode) {
  pinModeCalls().push_back(pin);
  bool isOut = (mode == OUTPUT);
  pinIsOutput()[pin] = isOut;
  if (isOut) pinWrites().push_back({pin, pinLatch()[pin]});
}

// Запись во входной пин меняет только защёлку: на ножке ничего не появится,
// пока она не станет выходом.
inline void digitalWrite(int pin, int level) {
  pinLatch()[pin] = level;
  if (pinIsOutput()[pin]) pinWrites().push_back({pin, level});
}

inline unsigned long millis() {
  return fakeMillis();
}
