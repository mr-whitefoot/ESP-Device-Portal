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

namespace portal {
constexpr settings::Key authEnabled{"portal.authEnabled"};
constexpr settings::Key username{"portal.username"};
constexpr settings::Key password{"portal.password"};
}  // namespace portal

namespace wifi {
constexpr settings::Key ssid{"wifiSsid"};
constexpr settings::Key password{"wifiPassword"};
// Ключа forceAP больше нет: он существовал только чтобы протащить намерение
// "подняться точкой доступа" через перезагрузку. Автомат держит состояние
// сам, перезагрузок в цикле подключения не осталось.
}  // namespace wifi

namespace mqtt {
// Имя, под которым устройство публиковалось до переименования. Живёт в
// настройках ровно потому, что должно пережить перезагрузку: снять старые
// retained-топики сразу нельзя -- перезагрузка рвёт соединение, и брокер по
// завещанию кладёт в старый avaible "offline", возвращая только что убранное.
// Пустое значение означает "нечего снимать".
constexpr settings::Key prevName{"mqtt.prevName"};
// Сущность в HomeAssistant нужно завести заново: uniq_id и object_id сменились
// в 3.6.0, а на живую сущность HA их не переносит. Живёт в настройках, чтобы
// пережить перезагрузку и недоступный в момент обновления брокер: уборка и
// объявление произойдут при первом же удачном подключении.
constexpr settings::Key rediscover{"mqtt.rediscover"};
constexpr settings::Key host{"mqtt.host"};
constexpr settings::Key port{"mqtt.port"};
constexpr settings::Key username{"mqtt.username"};
constexpr settings::Key password{"mqtt.password"};
constexpr settings::Key topicPrefix{"mqtt.topicPrefix"};
constexpr settings::Key statusDelay{"mqtt.statusDelay"};
constexpr settings::Key availableDelay{"mqtt.availableDelay"};
}  // namespace mqtt

}  // namespace keys
