#pragma once
#include <settings.h>
#include <timer_schedule.h>

// Параметры реле. Лежат рядом с устройством, а не в ядре: добавление нового
// устройства не должно требовать правки общих файлов.
//
// Имена ключей неприкасаемы -- переименование меняет хэш и равносильно
// потере параметра.

namespace keys {

namespace relay {
constexpr settings::Key invert{"relay.invert"};
constexpr settings::Key buttonMode{"relay.buttonMode"};
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
