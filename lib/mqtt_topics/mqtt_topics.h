#pragma once
#include <stddef.h>

// HomeAssistant разбирает discovery-топик регуляркой, в которой node_id и
// object_id допускают только [a-zA-Z0-9_-]. Имя устройства подставлялось в
// топик как есть, а по умолчанию оно "ESP Relay" -- с пробелом. Такой конфиг
// HA молча игнорирует, и автообнаружение просто не работает.
//
// Заголовок не зависит от Arduino, чтобы собираться в native-тестах.

inline bool isAllowedTopicChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// Приводит произвольное имя к безопасному сегменту топика.
// Подряд идущие замены схлопываются: иначе кириллическое имя, где каждый
// символ занимает несколько байт, превратилось бы в частокол подчёркиваний.
// Пустой результат заменяется на "ESP", чтобы в топике не появилось "//".
inline void sanitizeTopicSegment(const char* src, char* dst, size_t dstSize) {
  if (dst == nullptr || dstSize == 0) return;

  size_t n = 0;
  bool lastWasReplacement = false;

  for (; src != nullptr && *src != '\0' && n + 1 < dstSize; ++src) {
    if (isAllowedTopicChar(*src)) {
      dst[n++] = *src;
      lastWasReplacement = false;
    } else if (!lastWasReplacement) {
      dst[n++] = '_';
      lastWasReplacement = true;
    }
  }

  // Имя целиком из недопустимых символов схлопнулось бы в один "_".
  if ((n == 0 || (n == 1 && dst[0] == '_')) && dstSize > 3) {
    n = 0;
    for (const char* fb = "ESP"; *fb != '\0' && n + 1 < dstSize; ++fb) dst[n++] = *fb;
  }

  dst[n] = '\0';
}
