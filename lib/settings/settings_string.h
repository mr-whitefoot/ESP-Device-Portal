#pragma once
#include <Arduino.h>

#include "settings.h"

// Удобное чтение строкового параметра в String.
//
// Живёт отдельно от контракта и построено поверх него: сам слой намеренно
// работает с буфером вызывающего, чтобы не тянуть Arduino в native-тесты и
// не аллоцировать на каждое чтение. Здесь же аллокация допустима -- это
// путь для UI и для разовой настройки при загрузке, а не для горячего цикла.

namespace settings {

// 64 байта хватает: имя устройства, адрес брокера, SSID, пароль WPA2 (63).
inline String getStringValue(const Key& key, size_t maxLen = 64) {
  char buf[64];
  if (maxLen > sizeof(buf)) maxLen = sizeof(buf);
  getString(key, buf, maxLen);
  return String(buf);
}

}  // namespace settings
