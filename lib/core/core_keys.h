#pragma once
#include <settings.h>

// Ключи настроек, разложенные по владельцам.
//
// Здесь только то, что принадлежит ядру. Параметры конечного устройства
// живут рядом с ним самим -- см. lib/device_relay/relay_keys.h.
//
// Имена свободные: совместимость с ранее сохранёнными настройками
// сознательно не поддерживается, версия 3.1.0 сбрасывает их один раз.
// Дальше имена неприкасаемы -- переименование ключа меняет хэш и равносильно
// потере параметра.

namespace keys {

namespace sys {
// Версия раскладки настроек. Нужна, чтобы следующее несовместимое изменение
// можно было мигрировать, а не сбрасывать.
constexpr settings::Key schema{"sys.schema"};
}  // namespace sys

namespace dev {
constexpr settings::Key name{"dev.name"};
constexpr settings::Key timezone{"dev.timezone"};
}  // namespace dev

namespace wifi {
// ВНИМАНИЕ: имена обязаны совпадать с теми, что использует ESP-Relay-WiFi-lib
// (enum wifi : size_t в wifi_func.h). Хэш считается тем же алгоритмом, что
// у StringUtils SH, поэтому ключи сходятся, и библиотека с приложением видят
// одни и те же ячейки. Согласованность проверяется static_assert в main.cpp.
// После перехода на DBConnector ограничение снимется.
constexpr settings::Key ssid{"wifiSsid"};
constexpr settings::Key password{"wifiPassword"};
constexpr settings::Key forceAP{"wifiForceAP"};
}  // namespace wifi

namespace mqtt {
constexpr settings::Key host{"mqtt.host"};
constexpr settings::Key port{"mqtt.port"};
constexpr settings::Key username{"mqtt.username"};
constexpr settings::Key password{"mqtt.password"};
constexpr settings::Key topicPrefix{"mqtt.topicPrefix"};
constexpr settings::Key statusDelay{"mqtt.statusDelay"};
constexpr settings::Key availableDelay{"mqtt.availableDelay"};
}  // namespace mqtt

}  // namespace keys
