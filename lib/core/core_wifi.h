#pragma once
#include <ESP8266WiFi.h>

#include <core_keys.h>
#include <settings.h>
#include <settings_string.h>
#include <wifi_state.h>

// Привязка автомата WiFi к железу.
//
// Заменяет внешнюю ESP-Relay-WiFi-lib. Та жила отдельным репозиторием, из-за
// чего пять проектов разъезжались по её версиям, и держала собственную копию
// ключей настроек через SH() -- приходилось сверять хэши static_assert'ом.
//
// Здесь только исполнение: автомат решает, адаптер делает. Ничего, кроме
// перевода действий в вызовы SDK и портала, в этом файле быть не должно.

namespace corewifi {

WifiStateMachine machine;
String apName;

// Портал поднимается позже WiFi, поэтому до его старта трогать нельзя:
// ни спрашивать занятость, ни перезапускать.
bool portalReady = false;

inline String ssid() { return settings::getStringValue(keys::wifi::ssid); }

inline WifiInputs inputs() {
  WifiInputs in;
  in.hasCredentials = ssid().length() > 0;
  in.staConnected = (WiFi.status() == WL_CONNECTED);
  // online() держится 1.5 секунды с последнего запроса браузера. До старта
  // портала таймер нулевой, и первые полторы секунды аптайма он соврал бы.
  in.portalBusy = portalReady && portal.online();
  return in;
}

inline void startAttempt() {
  String name = ssid();
  println("WiFi connecting to " + name);

  // Если точка уже поднята, остаёмся в AP_STA: иначе оборвём и captive
  // portal, и сессию того, кто в этот момент сидит на странице.
  WiFi.mode(machine.apUp() ? WIFI_AP_STA : WIFI_STA);
  WiFi.hostname(settings::getStringValue(keys::dev::name));
  WiFi.begin(name, settings::getStringValue(keys::wifi::password));
}

inline void openAp() {
  println("Starting AP " + apName);

  // Портал перезапускается намеренно: признак captive portal защёлкивается
  // внутри start() и только при режиме ровно WIFI_AP. Если портал ещё не
  // поднят, перезапускать нечего -- portalStart() сам увидит нужный режим.
  if (portalReady) portal.stop();

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);

  println("AP IP address: " + WiFi.softAPIP().toString());
  if (portalReady) portal.start(settings::getStringValue(keys::dev::name).c_str());
}

inline void stopAttempt() {
  println("WiFi attempt failed, staying in AP");
  // Точка уже поднята, портал трогать нельзя -- иначе слетит captive portal.
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
}

inline void closeAp() {
  println("WiFi connected, closing AP");
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  // Перезапуск нужен, чтобы портал отпустил DNS и перепривязал mDNS к STA.
  if (portalReady) {
    portal.stop();
    portal.start(settings::getStringValue(keys::dev::name).c_str());
  }

  println("IP: " + WiFi.localIP().toString());
}

inline void apply(WifiAction action) {
  switch (action) {
    case WifiAction::StartAttempt: startAttempt(); break;
    case WifiAction::OpenAp:       openAp();       break;
    case WifiAction::StopAttempt:  stopAttempt();  break;
    case WifiAction::CloseAp:      closeAp();      break;
    case WifiAction::None:                         break;
  }
}

inline void begin(const String& deviceName) {
  println("-----------------------------");
  println("Initialize WiFi");

  apName = deviceName + " AP";
  apply(machine.begin(millis(), inputs()));

  // Первую попытку намеренно дожидаемся. Всё, что стартует следом --
  // ArduinoOTA, MQTT, NTP, портал -- инициализируется один раз, и должно
  // видеть уже определившуюся сеть. Для ESP-01 это критично: espota
  // единственный способ прошивки, и поднимать его в момент, когда интерфейс
  // ещё не готов, слишком рискованно.
  //
  // Ожидание только здесь. Дальше автомат работает из loop() и никого не
  // блокирует: ни повторы, ни возврат в точку доступа, ни новые креды.
  while (machine.state() == WifiState::Connecting) {
    delay(50);
    apply(machine.tick(millis(), inputs()));
  }
}

// Портал поднялся: с этого момента его можно перезапускать при смене режима.
inline void portalStarted() { portalReady = true; }

inline void tick() {
  apply(machine.tick(millis(), inputs()));
}

// Пользователь сохранил новые креды: пробуем немедленно, не дожидаясь паузы.
inline void credentialsChanged() {
  apply(machine.onCredentialsChanged(millis()));
}

// Для показа в портале: сколько осталось до следующей попытки.
inline uint32_t retryLeftSeconds() {
  return machine.retryLeftMs(millis()) / 1000;
}

inline bool apActive() { return machine.apUp(); }

}  // namespace corewifi
