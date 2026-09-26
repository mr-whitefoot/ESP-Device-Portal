#pragma once
#include <stdint.h>

// Отложенная перезагрузка.
//
// Тот же класс задачи, что и отложенное применение кредов в wifi_state.h, но
// другая половина: обработчик действий GyverPortal работает ДО отправки ответа
// (portal.h:221 -- сперва _action(), и только потом show()). Перезагрузка,
// вызванная прямо из обработчика, уносит с собой то самое соединение, по
// которому ответ должен уйти: страница после сохранения висит.
//
// Прежний restart() пробовал протолкнуть ответ вызовом portal.tick() перед
// ESP.restart(), но протолкнуть было нечего -- в момент работы обработчика
// ответ ещё не сформирован.
//
// Здесь только решение: когда наступил срок. Само выключение остаётся в
// адаптере (core_boot.h), а срок покрывается native-тестами.

// Пауза между просьбой перезагрузиться и самой перезагрузкой.
//
// Ответ уходит внутри того же portal.tick(), из которого вызван обработчик,
// то есть заведомо раньше следующего прохода loop(). Секунда взята с запасом
// на отправку и закрытие соединения и совпадает с WIFI_CREDENTIALS_DELAY_MS,
// откуда приём и заимствован; для пользователя она незаметна на фоне самой
// загрузки устройства.
static const uint32_t RESTART_DELAY_MS = 1000;

// Потолок ожидания закрытия MQTT при factory reset. Живое соединение
// закрывается за доли секунды; дольше -- значит брокер пропал, и в состоянии
// DISCONNECTING AsyncMqttClient не шлёт ping, так что разрыв пришлось бы ждать
// от lwIP минутами. Last Will в этом случае сработает и без нас.
static const uint32_t RESTART_MQTT_WAIT_MS = 10000;

enum class RestartMode : uint8_t {
  Normal,
  FactoryReset,
};

class RestartRequest {
 public:
  // Просьба перезагрузиться. Повторная не меняет срок: иначе поток запросов
  // (страница, которую пользователь переоткрывает, пока ждёт) отодвигал бы
  // перезагрузку бесконечно. Factory reset может только усилить уже заказанную
  // обычную перезагрузку: настройки к этому моменту стёрты, публиковать offline
  // и возвращать availability уже нельзя.
  void request(uint32_t now, RestartMode mode = RestartMode::Normal) {
    if (_pending) {
      if (mode == RestartMode::FactoryReset) _mode = mode;
      return;
    }
    _pending = true;
    _since = now;
    _mode = mode;
  }

  // Перезагрузка заказана и ещё не наступила. Нужно, чтобы не публиковать в
  // MQTT в этом окне: после factory reset очередное периодическое сообщение
  // вернуло бы брокеру топик, который только что сняли.
  bool pending() const { return _pending; }

  // Factory reset снимает retained-топики и завершает MQTT-сессию штатным
  // DISCONNECT. Перезагрузка до закрытия транспорта оборвала бы очередь и
  // разрешила брокеру опубликовать Last Will поверх снятого availability.
  bool waitsForMqtt() const { return _mode == RestartMode::FactoryReset; }

  // Обычная перезагрузка оставляет сущность существовать и переводит её в
  // offline. После factory reset availability уже снят и возвращать его нельзя.
  bool publishesOffline() const { return _mode == RestartMode::Normal; }

  // Один раз возвращает true, когда срок наступил.
  // mqttReady имеет значение только для factory reset: он ждёт подтверждённого
  // закрытия асинхронного транспорта, но не дольше RESTART_MQTT_WAIT_MS.
  bool tick(uint32_t now, bool mqttReady = true) {
    if (!_pending) return false;
    if (now - _since < RESTART_DELAY_MS) return false;
    if (waitsForMqtt() && !mqttReady && now - _since < RESTART_MQTT_WAIT_MS)
      return false;
    _pending = false;
    return true;
  }

 private:
  bool _pending = false;
  uint32_t _since = 0;
  RestartMode _mode = RestartMode::Normal;
};
