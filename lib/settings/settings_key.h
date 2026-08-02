#pragma once
#include <stdint.h>

// Идентификатор параметра.
//
// Строка имени нужна только на этапе компиляции -- для вычисления хэша.
// Если её не хранить, литерал выбрасывается линкером целиком, и осмысленные
// длинные имена не стоят ни байта флеша. Под -DSETTINGS_KEY_NAMES имя
// сохраняется: это нужно бэкендам, адресующимся строками (Preferences, JSON),
// и для отладочного дампа.
//
// Заголовок не зависит от Arduino, чтобы собираться в native-тестах.

namespace settings {

// Тот же алгоритм, что у StringUtils SH: hash = hash + (hash << 5) + c.
constexpr uint32_t keyHash(const char* s, uint32_t h = 0) {
  return *s ? keyHash(s + 1, h + (h << 5) + (uint32_t)(unsigned char)*s) : h;
}

struct Key {
#ifdef SETTINGS_KEY_NAMES
  const char* name;
#endif
  uint32_t id;

  constexpr explicit Key(const char* n)
#ifdef SETTINGS_KEY_NAMES
      : name(n), id(keyHash(n)) {
  }
#else
      : id(keyHash(n)) {
  }
#endif
};

}  // namespace settings
