#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <AsyncMqttClient.h>
#include <functional>
#include <mqtt_reconnect.h>

// Узкий адаптер асинхронного MQTT-клиента под нужды ядра.
//
// WiFi здесь только читается. Подключение к сети, режимы STA/AP/AP_STA,
// captive portal и повторные попытки WiFi остаются целиком в core_wifi.h.
// MQTT-транспорт не получает учётные данные WiFi и не может вызвать begin(),
// disconnect() или сменить режим радио.
class CoreMqttClient {
 public:
  using ConnectionCallback = std::function<void()>;
  using LogCallback = std::function<void(const String&)>;
  using MessageCallback = std::function<void(const String&)>;

  CoreMqttClient() {
    _client.onConnect([this](bool sessionPresent) {
      (void)sessionPresent;
      _connectedPending = true;
    });
    _client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
      _disconnectReason = reason;
      _disconnectedPending = true;
    });
    _client.onMessage(
        [this](char* topic, char* payload,
               AsyncMqttClientMessageProperties properties, size_t len,
               size_t index, size_t total) {
          (void)properties;
          receive(topic, reinterpret_cast<uint8_t*>(payload), len, index, total);
        });
  }

  void setMqttServer(const char* server, const char* username,
                     const char* password, uint16_t port) {
    _client.setServer(server, port);
    _client.setCredentials(username, password);
  }

  void setMqttClientName(const char* name) { _client.setClientId(name); }

  void enableLastWillMessage(const char* topic, const char* message,
                             bool retain) {
    _client.setWill(topic, 0, retain, message);
  }

  void enableDebuggingMessages(bool enabled = true) { _debug = enabled; }

  void setOnConnectionEstablishedCallback(ConnectionCallback callback) {
    _connectionCallback = callback;
  }

  void setLogCallback(LogCallback callback) { _logCallback = callback; }

  bool isConnected() const {
    return WiFi.status() == WL_CONNECTED && _client.connected();
  }

  bool publish(const String& topic, const String& payload,
               bool retain = false) {
    if (!isConnected()) return false;
    return _client.publish(topic.c_str(), 0, retain, payload.c_str()) != 0;
  }

  bool subscribe(const String& topic, MessageCallback callback,
                 uint8_t qos = 0) {
    _subscriptionTopic = topic;
    _messageCallback = callback;
    if (!isConnected()) return false;
    return _client.subscribe(topic.c_str(), qos) != 0;
  }

  // Никаких сетевых ожиданий внутри: AsyncClient только запускает попытку, а
  // завершение приходит callback-ом. Пользовательские callback-и исполняются
  // отсюда, в обычном Arduino loop(), а не из обработчика TCP.
  void loop() {
    // mqttClearRetained() и restart() исторически зовут loop() из callback-а
    // подключения. Повторно диспетчеризовать события нельзя. Асинхронный
    // транспорт отправляет очередь независимо от этого метода, а ранний
    // возврат сам по себе не гарантирует, что пакет успеет уйти до reboot.
    if (_insideLoop) {
      return;
    }

    _insideLoop = true;
    const uint32_t now = millis();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;

    if (_disconnectedPending) {
      _disconnectedPending = false;
      _reconnect.onDisconnected(now, wifiConnected);
      log("MQTT disconnected, reason " +
          String(static_cast<uint8_t>(_disconnectReason)));
    }

    if (_connectedPending) {
      _connectedPending = false;
      if (_client.connected() && _connectionCallback) _connectionCallback();
    }

    dispatchMessages();

    switch (_reconnect.tick(now, wifiConnected, _client.connected())) {
      case MqttReconnectAction::Connect:
        log("MQTT connecting");
        _client.connect();
        break;

      case MqttReconnectAction::Disconnect:
        // Только TCP/MQTT. WiFi API здесь намеренно не вызывается.
        if (wifiConnected) log("MQTT connection attempt timed out");
        _client.disconnect(true);
        break;

      case MqttReconnectAction::None:
        break;
    }

    _insideLoop = false;
  }

 private:
  static const uint8_t MESSAGE_QUEUE_SIZE = 4;

  void log(const String& text) {
    if (_logCallback) {
      _logCallback(text);
    } else if (_debug) {
      Serial.println(text);
    }
  }

  void receive(const char* topic, const uint8_t* payload, size_t len,
               size_t index, size_t total) {
    if (!_messageCallback || _subscriptionTopic != topic) return;

    if (index == 0) {
      _incomingPayload = "";
      _incomingPayload.reserve(total);
    }

    // Фрагменты должны идти подряд. Повреждённую/неполную команду безопаснее
    // отбросить, чем склеить в другую и переключить реле не тем значением.
    if (_incomingPayload.length() != index) {
      _incomingPayload = "";
      return;
    }

    _incomingPayload.concat(reinterpret_cast<const char*>(payload), len);
    if (index + len != total) return;

    if (_messageCount == MESSAGE_QUEUE_SIZE) {
      _messageOverflow = true;
      _incomingPayload = "";
      return;
    }

    _messageQueue[_messageTail] = _incomingPayload;
    _messageTail = (_messageTail + 1) % MESSAGE_QUEUE_SIZE;
    ++_messageCount;
    _incomingPayload = "";
  }

  void dispatchMessages() {
    if (_messageOverflow) {
      _messageOverflow = false;
      // Это аварийный предел: HomeAssistant присылает одну короткую команду
      // на действие. В serial сообщение остаётся даже без DEBUG_MQTT.
      Serial.println("MQTT command queue overflow");
    }

    while (_messageCount && _messageCallback) {
      String payload = _messageQueue[_messageHead];
      _messageQueue[_messageHead] = "";
      _messageHead = (_messageHead + 1) % MESSAGE_QUEUE_SIZE;
      --_messageCount;
      _messageCallback(payload);
    }
  }

  AsyncMqttClient _client;
  MqttReconnect _reconnect;
  ConnectionCallback _connectionCallback;
  LogCallback _logCallback;
  MessageCallback _messageCallback;
  String _subscriptionTopic;
  String _incomingPayload;
  String _messageQueue[MESSAGE_QUEUE_SIZE];
  uint8_t _messageHead = 0;
  uint8_t _messageTail = 0;
  uint8_t _messageCount = 0;
  bool _messageOverflow = false;
  bool _connectedPending = false;
  bool _disconnectedPending = false;
  bool _insideLoop = false;
  bool _debug = false;
  AsyncMqttClientDisconnectReason _disconnectReason =
      AsyncMqttClientDisconnectReason::TCP_DISCONNECTED;
};
