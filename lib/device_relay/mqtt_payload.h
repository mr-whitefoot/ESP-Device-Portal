#pragma once

// Разбор полезной нагрузки команды включения.
//
// В discovery объявлены pl_on/pl_off, и HomeAssistant публикует ровно их,
// но в командный топик пишут и руками, и другими клиентами. Прежний ToBool
// понимал только true/True/TRUE, а всё остальное, включая "ON" и "1",
// молча превращал в выключение.
//
// Заголовок не зависит от Arduino, чтобы собираться в native-тестах.

inline bool equalsIgnoreCaseAscii(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

// Нераспознанная команда возвращает fallback: неизвестная строка не повод
// выключать нагрузку, вызывающий передаёт сюда текущее состояние.
inline bool parseSwitchPayload(const char* payload, bool fallback) {
  static const char* const onWords[] = {"ON", "1", "TRUE", "YES"};
  static const char* const offWords[] = {"OFF", "0", "FALSE", "NO"};

  for (unsigned i = 0; i < sizeof(onWords) / sizeof(onWords[0]); i++)
    if (equalsIgnoreCaseAscii(payload, onWords[i])) return true;

  for (unsigned i = 0; i < sizeof(offWords) / sizeof(offWords[0]); i++)
    if (equalsIgnoreCaseAscii(payload, offWords[i])) return false;

  return fallback;
}
