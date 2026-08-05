#pragma once
#include <settings.h>
#include <timer_schedule.h>
#include <relay_bank_schedule.h>

// Имена ключей после выпуска менять нельзя: идентификатор -- compile-time хэш
// строки. Каналы и таймеры разложены по отдельным ячейкам без бинарных блобов.
namespace keys {
namespace relayBank {

constexpr settings::Key label[] = {
    settings::Key{"bank.relay0.label"}, settings::Key{"bank.relay1.label"},
    settings::Key{"bank.relay2.label"}, settings::Key{"bank.relay3.label"},
    settings::Key{"bank.relay4.label"}, settings::Key{"bank.relay5.label"},
    settings::Key{"bank.relay6.label"}, settings::Key{"bank.relay7.label"},
};
constexpr settings::Key invert[] = {
    settings::Key{"bank.relay0.invert"}, settings::Key{"bank.relay1.invert"},
    settings::Key{"bank.relay2.invert"}, settings::Key{"bank.relay3.invert"},
    settings::Key{"bank.relay4.invert"}, settings::Key{"bank.relay5.invert"},
    settings::Key{"bank.relay6.invert"}, settings::Key{"bank.relay7.invert"},
};
constexpr settings::Key buttonMode[] = {
    settings::Key{"bank.relay0.buttonMode"}, settings::Key{"bank.relay1.buttonMode"},
    settings::Key{"bank.relay2.buttonMode"}, settings::Key{"bank.relay3.buttonMode"},
    settings::Key{"bank.relay4.buttonMode"}, settings::Key{"bank.relay5.buttonMode"},
    settings::Key{"bank.relay6.buttonMode"}, settings::Key{"bank.relay7.buttonMode"},
};
constexpr settings::Key saveState[] = {
    settings::Key{"bank.relay0.saveState"}, settings::Key{"bank.relay1.saveState"},
    settings::Key{"bank.relay2.saveState"}, settings::Key{"bank.relay3.saveState"},
    settings::Key{"bank.relay4.saveState"}, settings::Key{"bank.relay5.saveState"},
    settings::Key{"bank.relay6.saveState"}, settings::Key{"bank.relay7.saveState"},
};
constexpr settings::Key state[] = {
    settings::Key{"bank.relay0.state"}, settings::Key{"bank.relay1.state"},
    settings::Key{"bank.relay2.state"}, settings::Key{"bank.relay3.state"},
    settings::Key{"bank.relay4.state"}, settings::Key{"bank.relay5.state"},
    settings::Key{"bank.relay6.state"}, settings::Key{"bank.relay7.state"},
};

static_assert(sizeof(label) / sizeof(label[0]) == RELAY_BANK_COUNT, "label keys");
static_assert(sizeof(invert) / sizeof(invert[0]) == RELAY_BANK_COUNT, "invert keys");
static_assert(sizeof(buttonMode) / sizeof(buttonMode[0]) == RELAY_BANK_COUNT, "button keys");
static_assert(sizeof(saveState) / sizeof(saveState[0]) == RELAY_BANK_COUNT, "save keys");
static_assert(sizeof(state) / sizeof(state[0]) == RELAY_BANK_COUNT, "state keys");

}  // namespace relayBank

namespace bankTimer {

constexpr settings::Key enable[] = {
    settings::Key{"bank.timer0.enable"}, settings::Key{"bank.timer1.enable"},
    settings::Key{"bank.timer2.enable"}, settings::Key{"bank.timer3.enable"},
    settings::Key{"bank.timer4.enable"},
};
constexpr settings::Key action[] = {
    settings::Key{"bank.timer0.action"}, settings::Key{"bank.timer1.action"},
    settings::Key{"bank.timer2.action"}, settings::Key{"bank.timer3.action"},
    settings::Key{"bank.timer4.action"},
};
constexpr settings::Key hours[] = {
    settings::Key{"bank.timer0.hours"}, settings::Key{"bank.timer1.hours"},
    settings::Key{"bank.timer2.hours"}, settings::Key{"bank.timer3.hours"},
    settings::Key{"bank.timer4.hours"},
};
constexpr settings::Key minutes[] = {
    settings::Key{"bank.timer0.minutes"}, settings::Key{"bank.timer1.minutes"},
    settings::Key{"bank.timer2.minutes"}, settings::Key{"bank.timer3.minutes"},
    settings::Key{"bank.timer4.minutes"},
};
constexpr settings::Key seconds[] = {
    settings::Key{"bank.timer0.seconds"}, settings::Key{"bank.timer1.seconds"},
    settings::Key{"bank.timer2.seconds"}, settings::Key{"bank.timer3.seconds"},
    settings::Key{"bank.timer4.seconds"},
};
constexpr settings::Key target[] = {
    settings::Key{"bank.timer0.target"}, settings::Key{"bank.timer1.target"},
    settings::Key{"bank.timer2.target"}, settings::Key{"bank.timer3.target"},
    settings::Key{"bank.timer4.target"},
};

static_assert(sizeof(enable) / sizeof(enable[0]) == TIMER_COUNT, "timer enable keys");
static_assert(sizeof(action) / sizeof(action[0]) == TIMER_COUNT, "timer action keys");
static_assert(sizeof(hours) / sizeof(hours[0]) == TIMER_COUNT, "timer hours keys");
static_assert(sizeof(minutes) / sizeof(minutes[0]) == TIMER_COUNT, "timer minute keys");
static_assert(sizeof(seconds) / sizeof(seconds[0]) == TIMER_COUNT, "timer second keys");
static_assert(sizeof(target) / sizeof(target[0]) == TIMER_COUNT, "timer target keys");

}  // namespace bankTimer
}  // namespace keys
