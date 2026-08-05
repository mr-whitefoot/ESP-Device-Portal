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

// Имя, которое отдаётся в portal.start(). Хранится целиком и на всё время
// работы: GyverPortal запоминает сырой указатель на эту строку в глобальный
// _gp_mdns и использует его позже при сборке страниц. Временный String из
// getStringValue давал висячий указатель.
String portalName;

// Портал поднимается позже WiFi, поэтому до его старта трогать нельзя:
// ни спрашивать занятость, ни перезапускать.
bool portalReady = false;

inline String ssid() { return settings::getStringValue(keys::wifi::ssid); }

// Есть ли SSID -- без аллокации. inputs() зовётся на каждом проходе loop(),
// а getStringValue строит String; сам слой настроек об этом предупреждает и
// пишет буфер вызывающего именно затем, чтобы горячий путь не аллоцировал.
inline bool hasSsid() {
  char probe[2];
  return settings::getString(keys::wifi::ssid, probe, sizeof(probe)) > 0;
}

inline WifiInputs inputs() {
  WifiInputs in;
  in.hasCredentials = hasSsid();
  in.staConnected = (WiFi.status() == WL_CONNECTED);
  // online() держится 1.5 секунды с последнего запроса браузера. До старта
  // портала таймер нулевой, и первые полторы секунды аптайма он соврал бы.
  in.portalBusy = portalReady && portal.online();
  // Спрашиваем SDK только когда точка действительно поднята.
  in.apHasClients = machine.apUp() && WiFi.softAPgetStationNum() > 0;
  return in;
}

inline void startAttempt() {
  String name = ssid();

  // AP_STA только пока на точке кто-то сидит.
  //
  // У ESP8266 одно радио на оба режима, и на слабом сигнале ассоциация в
  // AP_STA не проходит вообще. На железе это выглядело так: на загрузке
  // устройство подключалось (там режим чистый STA), а все последующие
  // повторы шли в AP_STA и проваливались бесконечно -- подняв точку, оно
  // само лишало себя возможности вернуться в сеть.
  //
  // Поэтому точка уступает место попытке, если ей никто не пользуется.
  // Ради живой сессии настройки она сохраняется: там человек важнее, а
  // повтор подождёт. Неудачная попытка вернёт режим AP через StopAttempt,
  // портал при этом не перезапускается и captive не теряется.
  bool keepAp = machine.apUp() && WiFi.softAPgetStationNum() > 0;

  // SSID последним: в нём бывают пробелы, и разбор строки по полям от этого
  // не должен ломаться.
  LOG_I(wifi, String(F("connecting mode=")) + (keepAp ? "AP_STA" : "STA") +
              F(" ssid=") + name);

  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.hostname(settings::getStringValue(keys::dev::name));
  WiFi.begin(name, settings::getStringValue(keys::wifi::password));
}

inline void openAp() {
  // Портал перезапускается намеренно: признак captive portal защёлкивается
  // внутри start() и только при режиме ровно WIFI_AP. Если портал ещё не
  // поднят, перезапускать нечего -- portalStart() сам увидит нужный режим.
  if (portalReady) portal.stop();

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);

  // Адрес и имя точки одной строкой: раздельными они всё равно читались
  // только вместе, а в кольцевом буфере портала занимали вдвое больше места.
  LOG_I(wifi, String(F("ap up ip=")) + WiFi.softAPIP().toString() +
              F(" name=") + apName);
  if (portalReady) portal.start(portalName.c_str());
}

inline void stopAttempt() {
  LOG_W(wifi, F("connect failed, staying in ap"));
  // Точка уже поднята, портал трогать нельзя -- иначе слетит captive portal.
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
}

inline void closeAp() {
  LOG_I(wifi, F("ap down"));

  // Один переход режима вместо двух. Здесь стоял ещё и softAPdisconnect(true),
  // а он внутри делает enableAP(false) -- ту же самую смену opmode
  // (ESP8266WiFiAP.cpp:314). Две подряд на живом соединении роняли
  // ассоциацию STA, и автомат тут же возвращался в точку доступа.
  WiFi.mode(WIFI_STA);

  // Перезапуск нужен, чтобы портал отпустил DNS: в режиме AP он поднимает
  // wildcard-резолвер на 53 порту, и в сети роутера такой сосед лишний.
  // Адрес здесь больше не печатается: его отдаёт строка о переходе в
  // Connected, и она приходит на любом пути подключения, а не только через
  // закрытие точки доступа.
  if (portalReady) {
    portal.stop();
    portal.start(portalName.c_str());
  }
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

// Прогон автомата с логом переходов.
//
// Логировать приходится здесь, а не в действиях: удачное подключение
// действия не порождает вовсе (_enterConnected возвращает None), и в
// обычной загрузке -- когда точка доступа не поднималась -- в логе не
// оставалось ни строки о том, что устройство в сети. Адрес печатал только
// closeAp(), то есть путь через точку доступа. Уход связи не печатал никто:
// он виден был лишь по внезапной строке "connecting" через десять секунд.
inline void applyTick(uint32_t now) {
  WifiState before = machine.state();
  apply(machine.tick(now, inputs()));
  WifiState after = machine.state();
  if (after == before) return;

  if (after == WifiState::Connected) {
    LOG_I(wifi, String(F("connected ip=")) + WiFi.localIP().toString() +
                F(" rssi=") + WiFi.RSSI());
  } else if (before == WifiState::Connected) {
    // Не сама пропажа несущей, а признание разрыва: автомат держит выдержку
    // WIFI_LOST_GRACE_MS, и строка означает, что связь не вернулась за неё.
    LOG_W(wifi, F("link lost"));
  }
}

inline void begin(const String& deviceName) {
  apName = deviceName + " AP";
  portalName = deviceName;

  // Без этого каждый WiFi.mode() и WiFi.begin() пишет настройки во флеш
  // (ESP8266WiFiGeneric.cpp:426: wifi_set_opmode против _current). Запись
  // останавливает процессор на десятки миллисекунд, а автомат меняет режим
  // при каждом переходе -- отсюда были провалы в отклике на секунды.
  // Хранить их там незачем: креды лежат в наших настройках, и WiFi.begin()
  // получает их явно на каждой попытке.
  WiFi.persistent(false);

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
    applyTick(millis());
  }
}

// Портал поднялся: с этого момента его можно перезапускать при смене режима.
inline void portalStarted() { portalReady = true; }

inline void tick() {
  applyTick(millis());
}

// Пользователь сохранил новые креды. Ничего не выполняем прямо здесь: вызов
// приходит из обработчика формы, а GyverPortal зовёт его ДО отправки ответа
// (portal.h:221). Смена режима радио оборвала бы соединение с браузером, и
// ответ бы не дошёл -- страница на железе так и висела. Автомат подождёт
// WIFI_CREDENTIALS_DELAY_MS и начнёт попытку из ближайшего tick().
inline void credentialsChanged() {
  machine.onCredentialsChanged(millis());
}

// Для показа в портале: сколько осталось до следующей попытки.
inline uint32_t retryLeftSeconds() {
  return machine.retryLeftMs(millis()) / 1000;
}

inline bool apActive() { return machine.apUp(); }

}  // namespace corewifi
