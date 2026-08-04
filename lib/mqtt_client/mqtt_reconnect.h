#pragma once
#include <stdint.h>

// Решение о подключении MQTT отделено от транспорта и WiFi-адаптера.
//
// Класс ничего не знает ни об Arduino, ни о радио: получает только факты о
// готовности WiFi и MQTT и возвращает действие. Благодаря этому MQTT не может
// самовольно менять режим WiFi, а паузы и переполнение millis проверяются на
// хосте.

static const uint32_t MQTT_INITIAL_CONNECT_DELAY_MS = 500;
static const uint32_t MQTT_RECONNECT_DELAY_MS = 15000;
static const uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;

enum class MqttReconnectAction : uint8_t {
  None,
  Connect,
  Disconnect,
};

class MqttReconnect {
 public:
  MqttReconnectAction tick(uint32_t now, bool wifiConnected,
                           bool mqttConnected) {
    if (!wifiConnected) {
      const bool mustDisconnect = _wifiConnected &&
                                  (mqttConnected || _attemptInProgress);
      _wifiConnected = false;
      _attemptInProgress = false;
      _scheduled = false;
      return mustDisconnect ? MqttReconnectAction::Disconnect
                            : MqttReconnectAction::None;
    }

    // Как и прежний EspMQTTClient, после появления WiFi даём стеку 500 мс
    // успокоиться и лишь затем начинаем подключение к брокеру.
    if (!_wifiConnected) {
      _wifiConnected = true;
      _attemptInProgress = false;
      schedule(now, MQTT_INITIAL_CONNECT_DELAY_MS);
    }

    if (mqttConnected) {
      _attemptInProgress = false;
      _scheduled = false;
      return MqttReconnectAction::None;
    }

    if (_attemptInProgress || !_scheduled || !due(now, _deadline)) {
      if (_attemptInProgress &&
          now - _attemptStarted >= MQTT_CONNECT_TIMEOUT_MS) {
        _attemptInProgress = false;
        schedule(now, MQTT_RECONNECT_DELAY_MS);
        return MqttReconnectAction::Disconnect;
      }
      return MqttReconnectAction::None;
    }

    _scheduled = false;
    _attemptInProgress = true;
    _attemptStarted = now;
    return MqttReconnectAction::Connect;
  }

  // Асинхронный транспорт сообщил об отказе или разрыве. Следующая попытка
  // начинается только по таймеру из обычного loop(), не из сетевого callback.
  void onDisconnected(uint32_t now, bool wifiConnected) {
    _attemptInProgress = false;
    _wifiConnected = wifiConnected;
    if (wifiConnected) {
      schedule(now, MQTT_RECONNECT_DELAY_MS);
    } else {
      _scheduled = false;
    }
  }

  // connect() может не принять даже постановку попытки, например при нехватке
  // памяти. Это такой же отказ, но callback транспорта в этом случае не будет.
  void onAttemptRejected(uint32_t now, bool wifiConnected) {
    onDisconnected(now, wifiConnected);
  }

 private:
  static bool due(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  void schedule(uint32_t now, uint32_t delay) {
    _deadline = now + delay;
    _scheduled = true;
  }

  bool _wifiConnected = false;
  bool _attemptInProgress = false;
  bool _scheduled = false;
  uint32_t _deadline = 0;
  uint32_t _attemptStarted = 0;
};
