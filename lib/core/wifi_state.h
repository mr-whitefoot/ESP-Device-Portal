#pragma once
#include <stdint.h>

// Автомат подключения к WiFi.
//
// Здесь только решения, без единого обращения к железу: на входе факты,
// которые наблюдает адаптер, на выходе действие, которое он должен выполнить.
// Благодаря этому все переходы, таймауты и гонки покрываются native-тестами,
// а на устройстве остаётся тонкая прослойка.
//
// Перезагрузок нет. Прежняя схема делала ESP.reset() и при неудачном
// подключении, и по таймеру точки доступа, потому что намерение приходилось
// протаскивать через перезагрузку флагом forceAP. Здесь есть состояние,
// поэтому флаг не нужен.
//
// Ключевое ограничение, вокруг которого построены действия: GyverPortal
// защёлкивает признак captive portal в момент portal.start() и только при
// режиме ровно WIFI_AP. Поэтому точка доступа поднимается ОДИН раз с
// перезапуском портала (OpenAp), а повторные попытки идут в WIFI_AP_STA
// (StartAttempt) -- DNS при этом продолжает работать, и клиентов портала
// не сбрасывает.

enum class WifiState : uint8_t {
  Connecting,  // идёт попытка подключения
  ApOnly,      // точка доступа поднята, ждём повтора или новых кредов
  Connected,   // подключены к роутеру
};

enum class WifiAction : uint8_t {
  None,
  StartAttempt,  // начать попытку: AP_STA если точка поднята, иначе STA
  OpenAp,        // поднять точку: остановить портал, WIFI_AP, запустить портал
  StopAttempt,   // прекратить попытку, остаться в чистом AP; портал НЕ трогать
  CloseAp,       // уйти в чистый STA с перезапуском портала
};

// Факты, которые адаптер снимает с железа и портала.
struct WifiInputs {
  bool hasCredentials;  // SSID в настройках не пуст
  bool staConnected;    // WiFi.status() == WL_CONNECTED
  bool portalBusy;      // на портале живой браузер (portal.online())
  // К точке доступа кто-то подключён (WiFi.softAPgetStationNum()).
  // Отдельно от portalBusy намеренно: portalBusy отвечает на вопрос "человек
  // сейчас что-то настраивает" и гаснет через полторы секунды после закрытия
  // вкладки, а этот -- на вопрос "выдернем ли мы связь, опустив точку".
  // Раньше закрытие точки решалось по portalBusy, и портал, открытый через
  // роутер, не давал её закрыть, а короткая пауза в опросе страницы опускала
  // точку прямо под подключённым клиентом.
  bool apHasClients;
};

// Сколько ждём подключения, прежде чем признать попытку неудачной.
static const uint32_t WIFI_ATTEMPT_TIMEOUT_MS = 15000;

// Нарастающие паузы между повторами. Если роутер просто перезагружался,
// ждать три минуты незачем, но и долбиться каждые полминуты сутками не нужно.
static const uint32_t WIFI_RETRY_STEPS_MS[] = {30000, 60000, 180000};
static const uint8_t WIFI_RETRY_STEP_COUNT =
    sizeof(WIFI_RETRY_STEPS_MS) / sizeof(WIFI_RETRY_STEPS_MS[0]);

// Связь может моргнуть, поэтому разрыв признаём не сразу.
static const uint32_t WIFI_LOST_GRACE_MS = 10000;

class WifiStateMachine {
 public:
  // Выбрать стартовое состояние. Возвращает первое действие.
  WifiAction begin(uint32_t now, const WifiInputs& in) {
    _apUp = false;
    _retryStep = 0;
    _lostSeen = false;

    if (!in.hasCredentials) return _enterAp(now);
    return _startAttempt(now);
  }

  // Пользователь сохранил новые креды: пробуем немедленно, не дожидаясь
  // паузы, и сбрасываем счётчик повторов -- это новая попытка, не продолжение
  // прежних неудач.
  WifiAction onCredentialsChanged(uint32_t now) {
    _retryStep = 0;
    _lostSeen = false;
    return _startAttempt(now);
  }

  WifiAction tick(uint32_t now, const WifiInputs& in) {
    switch (_state) {
      case WifiState::Connecting: return _tickConnecting(now, in);
      case WifiState::ApOnly:     return _tickApOnly(now, in);
      case WifiState::Connected:  return _tickConnected(now, in);
    }
    return WifiAction::None;
  }

  WifiState state() const { return _state; }
  bool apUp() const { return _apUp; }

  // Сколько осталось до следующей попытки, для показа в портале.
  // Ноль, если ждать нечего.
  uint32_t retryLeftMs(uint32_t now) const {
    if (_state != WifiState::ApOnly) return 0;
    uint32_t delay = _retryDelay();
    uint32_t passed = now - _since;
    return (passed >= delay) ? 0 : (delay - passed);
  }

 private:
  WifiState _state = WifiState::ApOnly;
  bool _apUp = false;
  uint32_t _since = 0;      // когда вошли в текущее состояние
  bool _lostSeen = false;   // связь пропала, отсчитываем выдержку
  uint32_t _lostSince = 0;  // когда впервые заметили пропажу
  // Номер повтора для нарастающей паузы. Увеличивается при СТАРТЕ повтора,
  // а не при неудаче: иначе первое ожидание брало бы вторую ступень.
  uint8_t _retryStep = 0;

  uint32_t _retryDelay() const {
    uint8_t i = (_retryStep < WIFI_RETRY_STEP_COUNT) ? _retryStep
                                                     : (WIFI_RETRY_STEP_COUNT - 1);
    return WIFI_RETRY_STEPS_MS[i];
  }

  WifiAction _startAttempt(uint32_t now) {
    _state = WifiState::Connecting;
    _since = now;
    return WifiAction::StartAttempt;
  }

  WifiAction _enterAp(uint32_t now) {
    _state = WifiState::ApOnly;
    _since = now;

    if (_apUp) return WifiAction::StopAttempt;  // портал уже с captive, не трогаем
    _apUp = true;
    return WifiAction::OpenAp;
  }

  WifiAction _enterConnected(uint32_t now) {
    _state = WifiState::Connected;
    _since = now;
    _retryStep = 0;
    _lostSeen = false;
    return WifiAction::None;
  }

  WifiAction _tickConnecting(uint32_t now, const WifiInputs& in) {
    if (in.staConnected) return _enterConnected(now);

    if (now - _since >= WIFI_ATTEMPT_TIMEOUT_MS) return _enterAp(now);
    return WifiAction::None;
  }

  WifiAction _tickApOnly(uint32_t now, const WifiInputs& in) {
    // Пробовать нечего: без SSID повторы бессмысленны, ждём новых кредов.
    if (!in.hasCredentials) return WifiAction::None;

    if (now - _since < _retryDelay()) return WifiAction::None;

    // Не дёргаем радио, пока человек настраивает устройство через портал:
    // переход в AP_STA способен сбить его сессию. Ждём, пока освободится.
    if (in.portalBusy) return WifiAction::None;

    if (_retryStep < WIFI_RETRY_STEP_COUNT) _retryStep++;
    return _startAttempt(now);
  }

  WifiAction _tickConnected(uint32_t now, const WifiInputs& in) {
    if (!in.staConnected) {
      // Связь могла моргнуть, поэтому разрыв признаём не сразу.
      if (!_lostSeen) {
        _lostSeen = true;
        _lostSince = now;
      }
      if (now - _lostSince >= WIFI_LOST_GRACE_MS) {
        _lostSeen = false;
        _retryStep = 0;

        // Сначала пробуем подключиться заново и только потом поднимаем точку.
        // Прежняя версия шла в AP сразу, а подъём точки меняет режим радио и
        // роняет связь ещё раз -- получался самоподдерживающийся цикл
        // AP -> повтор -> срыв -> AP, в котором устройство было доступно
        // секунды из каждой минуты.
        return _startAttempt(now);
      }
      return WifiAction::None;
    }

    _lostSeen = false;

    // Точка доступа остаётся поднятой после удачного повтора. Закрываем её,
    // но не выдёргиваем связь у того, кто прямо сейчас к ней подключён.
    if (_apUp && !in.apHasClients) {
      _apUp = false;
      return WifiAction::CloseAp;
    }
    return WifiAction::None;
  }
};
