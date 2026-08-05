#pragma once
#include <stdint.h>

// Чистая часть конфигурации банка, используемая и прошивкой, и native-тестами.
static const uint8_t RELAY_BANK_COUNT = 8;
static const char* const RELAY_BANK_ENTITY_IDS[RELAY_BANK_COUNT] = {
    "relay_1", "relay_2", "relay_3", "relay_4",
    "relay_5", "relay_6", "relay_7", "relay_8",
};

inline uint8_t relayBankTarget(int32_t value) {
  return value >= 0 && value < RELAY_BANK_COUNT
      ? static_cast<uint8_t>(value)
      : 0;
}
