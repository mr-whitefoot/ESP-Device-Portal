#pragma once
#include <stdint.h>

// Чистая часть конфигурации банка, используемая и прошивкой, и native-тестами.
static const uint8_t RELAY_BANK_COUNT = 8;
// Значения 0..7 уже хранятся в настройках выпущенной прошивки как номера
// каналов. Групповая цель добавлена следующим значением, чтобы не сдвигать их.
static const uint8_t RELAY_BANK_ALL_TARGET = RELAY_BANK_COUNT;
static const char* const RELAY_BANK_ENTITY_IDS[RELAY_BANK_COUNT] = {
    "relay_1", "relay_2", "relay_3", "relay_4",
    "relay_5", "relay_6", "relay_7", "relay_8",
};

inline uint8_t relayBankTarget(int32_t value) {
  return value >= 0 && value <= RELAY_BANK_ALL_TARGET
      ? static_cast<uint8_t>(value)
      : 0;
}

inline bool relayBankTargetIncludes(uint8_t target, uint8_t relay) {
  return relay < RELAY_BANK_COUNT &&
         (target == RELAY_BANK_ALL_TARGET || target == relay);
}
