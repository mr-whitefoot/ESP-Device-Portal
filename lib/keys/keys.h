#pragma once
#include <settings.h>
#include <timer_schedule.h>

// Ключи настроек, разложенные по владельцам.
//
// Каждая группа принадлежит своему модулю: при выделении ядра и контракта
// устройства keys::relay и keys::timer уедут к реле, остальное останется
// в ядре. Пока лежат рядом, но уже разделены namespace'ами, чтобы переезд
// был перемещением текста, а не разбором зависимостей.
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

namespace relay {
constexpr settings::Key invert{"relay.invert"};
constexpr settings::Key saveState{"relay.saveState"};
constexpr settings::Key state{"relay.state"};
}  // namespace relay

namespace timer {
// Каждое поле отдельной ячейкой вместо двоичного блоба всей структуры.
// Блоб экономил около сотни байт и ломался при первом же изменении раскладки:
// шестой таймер или новое поле делали сохранённые настройки нечитаемыми.
constexpr settings::Key enable[] = {
    settings::Key{"timer0.enable"}, settings::Key{"timer1.enable"},
    settings::Key{"timer2.enable"}, settings::Key{"timer3.enable"},
    settings::Key{"timer4.enable"},
};
constexpr settings::Key action[] = {
    settings::Key{"timer0.action"}, settings::Key{"timer1.action"},
    settings::Key{"timer2.action"}, settings::Key{"timer3.action"},
    settings::Key{"timer4.action"},
};
constexpr settings::Key hours[] = {
    settings::Key{"timer0.hours"}, settings::Key{"timer1.hours"},
    settings::Key{"timer2.hours"}, settings::Key{"timer3.hours"},
    settings::Key{"timer4.hours"},
};
constexpr settings::Key minutes[] = {
    settings::Key{"timer0.minutes"}, settings::Key{"timer1.minutes"},
    settings::Key{"timer2.minutes"}, settings::Key{"timer3.minutes"},
    settings::Key{"timer4.minutes"},
};
constexpr settings::Key seconds[] = {
    settings::Key{"timer0.seconds"}, settings::Key{"timer1.seconds"},
    settings::Key{"timer2.seconds"}, settings::Key{"timer3.seconds"},
    settings::Key{"timer4.seconds"},
};

// Добавили таймер в TIMER_COUNT -- добавьте ключи, иначе он молча не сохранится.
static_assert(sizeof(enable) / sizeof(enable[0]) == TIMER_COUNT, "не хватает ключей enable");
static_assert(sizeof(action) / sizeof(action[0]) == TIMER_COUNT, "не хватает ключей action");
static_assert(sizeof(hours) / sizeof(hours[0]) == TIMER_COUNT, "не хватает ключей hours");
static_assert(sizeof(minutes) / sizeof(minutes[0]) == TIMER_COUNT, "не хватает ключей minutes");
static_assert(sizeof(seconds) / sizeof(seconds[0]) == TIMER_COUNT, "не хватает ключей seconds");
}  // namespace timer

}  // namespace keys
