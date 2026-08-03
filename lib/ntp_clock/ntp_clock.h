#pragma once
#include <stdint.h>

// Часы, синхронизируемые по NTP, без единого блокирующего вызова.
//
// Прежний NTPClient ждал ответ на месте: forceUpdate() крутил delay(10) и
// сдавался через 1000 мс. Пул отдаёт разные машины, и доставшаяся устройству
// оказалась мёртвой -- раз в минуту loop() замирал на секунду целиком. Замеры
// на железе показывали 1023 мс в этом вызове и падение числа проходов loop()
// с 6800 до 2900 в секунду; портал в это окно не отвечал, OTA не обслуживался.
//
// Ограничение частоты (раз в минуту вместо каждого прохода) саму блокировку не
// убирало, а закрепление отрезолвленного адреса её усилило: негодный сервер
// держался десять попыток подряд. Поэтому здесь запрос и ответ разнесены во
// времени -- ровно тот же приём, что уже применён к чтению DS18B20. Ждать
// нечего: ответ подберётся в одном из следующих проходов loop().
//
// Заголовок не зависит от Arduino, чтобы собираться в native-тестах: сеть
// живёт в адаптере, здесь только решения и счёт времени.

namespace ntp {

// Что адаптеру сделать прямо сейчас.
enum class Action : uint8_t {
  Nothing,
  Resolve,  // адреса сервера нет либо прежний себя не оправдал
  Send,     // отправить запрос на известный адрес
};

// Между удачными синхронизациями. Кварц ESP уходит на единицы ppm, за десять
// минут это доли секунды -- незаметно для таймеров с секундной сеткой.
static const uint32_t SYNC_INTERVAL_MS = 600UL * 1000UL;

// После неудачи. Пауза короткая намеренно: ожидание теперь ничего не стоит,
// а неспешные повторы означали бы, что устройство долго живёт без времени.
static const uint32_t RETRY_INTERVAL_MS = 15UL * 1000UL;

// Сколько ждать ответа, прежде чем считать попытку неудачной. Ожидание
// пассивное, всё это время loop() работает как обычно, поэтому запас щедрый:
// сервер из пула отвечает за сотню миллисекунд.
static const uint32_t RESPONSE_TIMEOUT_MS = 2000;


class NtpClock {
 public:
  // Смещение часового пояса в секундах. Меняется из портала на лету и на
  // синхронизацию не влияет: хранится отдельно от полученного UTC.
  void setOffset(int32_t seconds) { _offset = seconds; }
  int32_t offset() const { return _offset; }

  bool isTimeSet() const { return _synced; }

  // Факты на входе, действие на выходе -- как в автомате WiFi. Само по себе
  // tick() ничего не ждёт и вернуться обязано немедленно.
  Action tick(uint32_t nowMs, bool linkUp) {
    if (!linkUp) {
      // Без сети ждать нечего: незакрытый запрос иначе досидел бы до таймаута
      // уже после восстановления связи и сжёг бы лишний цикл повтора.
      _pending = false;
      _resolving = false;
      return Action::Nothing;
    }

    if (_pending) {
      if (!reached(nowMs, _pendingSince + RESPONSE_TIMEOUT_MS)) return Action::Nothing;

      // Сервер не ответил -- адрес не закрепляем. Ровно на этом обжигалась
      // прежняя версия: мёртвая машина из пула держалась десять попыток.
      _pending = false;
      _haveServer = false;
      _nextAttempt = nowMs + RETRY_INTERVAL_MS;
      return Action::Nothing;
    }

    // Резолв тоже асинхронный: пока он идёт, делать нечего.
    if (_resolving) return Action::Nothing;

    // Срок первой попытки берётся из первого же вызова, а не из нуля: сравнение
    // моментов знаковое, и объект, начавший жить при millis больше 2^31, с
    // нулём в качестве срока ждал бы полсуток вместо того, чтобы взяться сразу.
    if (!_begun) {
      _begun = true;
      _nextAttempt = nowMs;
    }

    if (!reached(nowMs, _nextAttempt)) return Action::Nothing;

    if (!_haveServer) {
      _resolving = true;
      return Action::Resolve;
    }

    _pending = true;
    _pendingSince = nowMs;
    return Action::Send;
  }

  void onResolved(uint32_t nowMs, bool ok) {
    if (!_resolving) return;
    _resolving = false;
    _haveServer = ok;
    // При удаче запрос уйдёт ближайшим tick(), паузу назначаем только на отказ.
    if (!ok) _nextAttempt = nowMs + RETRY_INTERVAL_MS;
  }

  void onResponse(uint32_t nowMs, uint32_t epochSeconds) {
    // Незапрошенный или опоздавший после таймаута пакет игнорируется: принять
    // его значило бы поверить в любой прилетевший на порт мусор.
    if (!_pending) return;

    _pending = false;
    _haveServer = true;  // сервер себя оправдал
    _synced = true;
    _epochAtSync = epochSeconds;
    _msAtSync = nowMs;
    _nextAttempt = nowMs + SYNC_INTERVAL_MS;
  }

  // UTC. До первой синхронизации ноль, но спрашивать об этом должен
  // isTimeSet(): ноль это 1970 год, а не "не знаю".
  uint32_t epoch(uint32_t nowMs) const {
    if (!_synced) return 0;
    return _epochAtSync + (nowMs - _msAtSync) / 1000UL;
  }

  // Секунда местных суток.
  //
  // Смещение прибавляется со знаком и только к остатку от суток. Прибавить его
  // беззнаково ко всей эпохе нельзя: 2^32 не делится на 86400, и переход через
  // ноль сдвинул бы результат на 23296 секунд -- отрицательные пояса показывали
  // бы чужое время. Остаток заведомо меньше 86400, пояс не больше 14 часов,
  // поэтому сумма укладывается в int32 без риска.
  uint32_t secondOfDay(uint32_t nowMs) const {
    int32_t local = (int32_t)(epoch(nowMs) % 86400UL) + _offset;
    local %= 86400;
    if (local < 0) local += 86400;
    return (uint32_t)local;
  }

  uint8_t hours(uint32_t nowMs) const   { return (uint8_t)(secondOfDay(nowMs) / 3600UL); }
  uint8_t minutes(uint32_t nowMs) const { return (uint8_t)((secondOfDay(nowMs) / 60UL) % 60UL); }
  uint8_t seconds(uint32_t nowMs) const { return (uint8_t)(secondOfDay(nowMs) % 60UL); }

  // "ЧЧ:ММ:СС" в буфер вызывающего: та же причина, что у settings::getString --
  // тестируется нативно и не аллоцирует. Буфер обязан быть не меньше девяти.
  void formatTime(uint32_t nowMs, char* out) const {
    uint32_t s = secondOfDay(nowMs);
    two(out,     (uint8_t)(s / 3600UL));
    out[2] = ':';
    two(out + 3, (uint8_t)((s / 60UL) % 60UL));
    out[5] = ':';
    two(out + 6, (uint8_t)(s % 60UL));
    out[8] = '\0';
  }

 private:
  // Сравнение моментов, переживающее переполнение millis (каждые ~49.7 суток):
  // разность беззнаковых, приведённая к знаковому, верна на всём диапазоне.
  static bool reached(uint32_t nowMs, uint32_t momentMs) {
    return (int32_t)(nowMs - momentMs) >= 0;
  }

  static void two(char* out, uint8_t v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
  }

  int32_t _offset = 0;

  bool _synced = false;
  uint32_t _epochAtSync = 0;
  uint32_t _msAtSync = 0;

  bool _haveServer = false;
  bool _resolving = false;
  bool _pending = false;
  uint32_t _pendingSince = 0;

  // Срок следующей попытки. Осмысленное значение появляется в первом tick(),
  // см. _begun: до этого сравнивать не с чем.
  uint32_t _nextAttempt = 0;
  bool _begun = false;
};

}  // namespace ntp
