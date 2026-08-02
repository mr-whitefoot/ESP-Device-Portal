#pragma once
#include <stdint.h>

// Расписание суточных таймеров.
//
// Раньше срабатывание проверялось точным равенством ЧЧ:ММ:СС на тике в 1000 мс.
// Тик loop() плавает: переподключение MQTT, отдача страницы портала или OTA
// съедали секунду, совпадения не случалось, и таймер молча не срабатывал.
// Обратный случай -- два тика внутри одной секунды -- давал двойное Toggle.
//
// Здесь вместо равенства проверяется попадание в полуинтервал
// (предыдущая проверка, текущая секунда]: событие не теряется при пропуске
// и не повторяется при частых вызовах.
//
// Заголовок не зависит от Arduino, чтобы собираться в native-тестах.

static const uint8_t TIMER_COUNT = 5;
static const uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;

// Дольше этого промежутка пропущенные события не догоняются. После долгого
// обрыва связи или первой синхронизации NTP отрабатывать разом всё, что
// прошло за час, хуже, чем не отработать вовсе.
static const uint32_t TIMER_MAX_CATCHUP = 300;

enum TimerAction : uint8_t {
  TIMER_ACTION_ON = 0,
  TIMER_ACTION_OFF = 1,
  TIMER_ACTION_TOGGLE = 2,
};

struct Timer {
  bool enable;
  uint8_t action;
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
};

struct Timers {
  Timer timer[TIMER_COUNT];
};

// Раскладка структуры больше ни на что не влияет: каждое поле хранится
// отдельной ячейкой настроек (см. keys::timer), а не двоичным слепком всей
// структуры. Раньше слепок уходил в базу как есть, и добавление таймера или
// поля делало сохранённые настройки нечитаемыми.

inline uint32_t timerSecondOfDay(const Timer& t) {
  return (uint32_t)t.hours * 3600UL + (uint32_t)t.minutes * 60UL + (uint32_t)t.seconds;
}

class TimerScheduler {
 public:
  // Сбрасывает точку отсчёта. Ближайший due() только запомнит время и
  // ничего не вернёт: иначе после скачка часов сработало бы всё подряд.
  void resync() { _started = false; }

  // Битовая маска таймеров, чьё время попало в (предыдущий вызов, now].
  uint32_t due(const Timers& timers, uint32_t now) {
    if (now >= SECONDS_PER_DAY) return 0;

    if (!_started) {
      _started = true;
      _last = now;
      return 0;
    }
    if (now == _last) return 0;

    // Ход назад неотличим от перехода через полночь только по значению,
    // поэтому решает величина промежутка: сутки минус секунда это скачок
    // часов, а не наступившая полночь.
    uint32_t elapsed = (now > _last) ? (now - _last) : (SECONDS_PER_DAY - _last + now);
    if (elapsed > TIMER_MAX_CATCHUP) {
      _last = now;
      return 0;
    }

    uint32_t mask = 0;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
      const Timer& t = timers.timer[i];
      if (!t.enable) continue;

      uint32_t at = timerSecondOfDay(t);
      if (at >= SECONDS_PER_DAY) continue;

      bool hit = (_last < now) ? (at > _last && at <= now)
                               : (at > _last || at <= now);
      if (hit) mask |= (1UL << i);
    }

    _last = now;
    return mask;
  }

 private:
  bool _started = false;
  uint32_t _last = 0;
};
