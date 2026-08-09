#include <restart_request.h>

// Заказанная, но ещё не наступившая перезагрузка. Разбор -- в restart_request.h
// и у restartRequest() ниже.
RestartRequest restartPending;

// GyverPortal запоминает переданные в enableAuth() const char*, поэтому строки
// должны жить всё время работы портала, а не быть временными результатами
// settings::getStringValue().
String portalAuthUsername;
String portalAuthPassword;


// Имя режима флеша для баннера. Числом было бы дешевле, но строку из лога
// читает человек, а не программа.
const char* flashModeName(){
  switch (ESP.getFlashChipMode()) {
    case FM_QIO:  return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO:  return "DIO";
    case FM_DOUT: return "DOUT";
    default:      return "?";
  }
}


void portalStart(){
  portal.attachBuild(portalBuild);

  portalAuthUsername = settings::getStringValue(keys::portal::username);
  portalAuthPassword = settings::getStringValue(keys::portal::password);
  bool portalAuthEnabled = settings::getBool(keys::portal::authEnabled) &&
                           portalAuthUsername.length() &&
                           portalAuthPassword.length();
  if (portalAuthEnabled) {
    portal.enableAuth(portalAuthUsername.c_str(), portalAuthPassword.c_str());
  } else {
    portal.disableAuth();
  }
  portal.attach(portalAction);
  // Имя берётся из corewifi: portal.start() запоминает сырой указатель, и
  // строка обязана пережить вызов. Временный String из getStringValue умирал
  // в конце выражения.
  portal.start(corewifi::portalName.c_str());
  // После start(): маршруты регистрируются на сервере портала, а его создаёт
  // start(). Свой обработчик имеет приоритет над onNotFound, который иначе
  // принял бы /fw.js за файл в файловой системе.
  coreupdate::routes();
  // Приём образа проверяет учётные данные сам: onNotFound, который закрывает
  // остальные страницы, до отдельного маршрута не доходит. Пустые строки при
  // выключенной авторизации означают открытый приём -- как и весь портал.
  if (portalAuthEnabled)
    coreota::routes(portalAuthUsername, portalAuthPassword);
  else
    coreota::routes("", "");
  corewifi::portalStarted();

  LOG_I(web, String(F("portal up auth=")) + (portalAuthEnabled ? "on" : "off"));
}


// Опрос времени живёт в core_ntp.h: там разнесённые во времени запрос и ответ,
// здесь только точка входа из loop(). Прежний блок с NTPClient снят целиком --
// его update() ждал ответ на месте до 1000 мс, и с неотвечающим сервером из
// пула это была секунда мёртвого loop() каждую минуту.
void ntpTick(){
  corentp::tick();
}


// Версия раскладки настроек. Поднимается только при несовместимом изменении,
// и тогда же сюда добавляется ветка миграции. Добавление нового параметра
// версию НЕ меняет: define сам подставит значение по умолчанию, не тронув
// остальные -- ради этого свойства слой и заводился.
static const int32_t SETTINGS_SCHEMA = 2;


void settingsMigrate(){
  int32_t schema = settings::getInt(keys::sys::schema);
  if(schema == SETTINGS_SCHEMA) return;

  if(schema == 0){
    // Либо чистое устройство, либо прошивка до 3.1.0, где ключи назывались
    // иначе. Во втором случае старые ячейки всё равно недостижимы, и оставлять
    // их значит вечно носить около трёхсот байт мусора в файле базы.
    LOG_I(set, F("schema=0 starting fresh"));
    settings::clear();
  }

  if(schema == 1){
    // 3.6.0. Сброса не требуется -- ни один параметр не сменил ни имени, ни
    // смысла. Меняется то, что устройство рассказывает о себе брокеру, и
    // прежнюю сущность в HomeAssistant надо снять до объявления новой:
    // разбор в mqttRetirePreviousEntity().
    //
    // Свежему устройству это не нужно, поэтому ветка отдельная от нулевой:
    // снимать там нечего, а лишний пустой config означал бы три секунды
    // задержки автообнаружения на ровном месте.
    LOG_I(set, F("schema=1 to=2 entity will be re-announced"));
    settings::defineBool(keys::mqtt::rediscover, false);
    settings::setBool(keys::mqtt::rediscover, true);

    // Периодическое available раз в минуту -- наследие времён до завещания:
    // доступность публикуется retained, а об уходе устройства брокер
    // рассказывает сам. Поднимаем только нетронутое значение по умолчанию:
    // свой период пользователь ставил осознанно, и перебивать его нельзя.
    if(settings::getInt(keys::mqtt::availableDelay) == 60)
      settings::setInt(keys::mqtt::availableDelay, 300);
  }

  settings::setInt(keys::sys::schema, SETTINGS_SCHEMA);
  settings::commit();
}


void settingsSetup(){
  // Уровень E, а не просто сообщение в Serial: не открывшаяся база означает,
  // что устройство поднимется на значениях по умолчанию и молча забудет всё
  // настроенное. Это худшее, что случается на загрузке, и в логе оно должно
  // выглядеть соответственно.
  if (!settings::begin()) LOG_E(set, F("storage init failed"));

  // Миграция до значений по умолчанию: она может очистить базу, и defineX
  // ниже наполнят её заново.
  settingsMigrate();

  // define создаёт параметр, только если его ещё нет, и никогда не трогает
  // сохранённое. Именно поэтому новый параметр в следующей прошивке получит
  // значение по умолчанию, а остальные переживут обновление.
  settings::defineString(keys::dev::name, (String("ESP ") + device::model()).c_str());
  settings::defineInt(keys::dev::timezone, TIMEZONE_UTC);

  // После обновления портал остаётся открытым, пока владелец сам не задаст
  // учётные данные и не включит авторизацию в Preferences.
  settings::defineBool(keys::portal::authEnabled, false);
  settings::defineString(keys::portal::username, "");
  settings::defineString(keys::portal::password, "");

  settings::defineString(keys::mqtt::topicPrefix, "homeassistant");
  // Пусто -- значит за переименованием убирать нечего.
  settings::defineString(keys::mqtt::prevName, "");
  settings::defineInt(keys::mqtt::port, 1883);
  settings::defineInt(keys::mqtt::statusDelay, 10);
  // Пять минут, а не минута: доступность публикуется retained и подкреплена
  // завещанием, так что периодическое сообщение здесь -- страховка, а не
  // основной механизм.
  settings::defineInt(keys::mqtt::availableDelay, 300);
  // Пусто -- значит объявляться заново незачем, сущность уже актуальна.
  settings::defineBool(keys::mqtt::rediscover, false);

  device::defineSettings();

  settings::defineString(keys::wifi::ssid, "");
  settings::defineString(keys::wifi::password, "");

  #ifdef DEBUG_DB
    settings::detail::db().dump(Serial);
  #endif
}


// Обновление по воздуху -- единственный способ прошить ESP-01, и до сих пор
// оно не оставляло в логе ни следа. Незаконченная попытка выглядела как
// беспричинная потеря отзывчивости, а законченная -- как беспричинная
// перезагрузка. Успех отдельной строкой не отмечается: о нём говорит
// загрузочный баннер новой версии.
void otaLogSetup(){
  ArduinoOTA.onStart([](){ LOG_W(ota, F("update started, device is busy")); });
  ArduinoOTA.onError([](ota_error_t error){
    LOG_E(ota, String(F("update failed code=")) + error);
  });
}


void startup(){
  Serial.begin(74880);
  //Log
  glog.start(1000);

  // Пустая строка отделяет наш вывод от сообщений загрузчика: он говорит на
  // тех же 74880 бодах и обрывается на середине строки.
  Serial.println();

  // Первым делом -- кто загрузился и почему. Это единственные две строки,
  // по которым разбирается самый частый вопрос к устройству без консоли:
  // "оно перезагрузилось само, что случилось". Причина сброса живёт только
  // до следующей перезагрузки, поэтому спросить её потом уже нельзя.
  LOG_I(boot, String(F("start ")) + device::model() + " v" + sw_version +
              F(" built=") + release_date);
  // Режим флеша в баннере -- единственный способ узнать его, не снимая
  // устройство: неверный режим убивает плату не сразу, а при следующей смене
  // SDK, и тогда её уже не спросишь по сети. Разбор -- в HANDOFF.md.
  LOG_I(boot, String(F("chip=")) + String(ESP.getChipId(), HEX) +
              F(" heap=") + ESP.getFreeHeap() +
              F(" flash=") + flashModeName() +
              F(" reset=") + ESP.getResetReason());

  //Settings
  settingsSetup();

  //Device
  device::begin();

  // WiFi
  corewifi::begin(settings::getStringValue(keys::dev::name));

  // Enable OTA update
  otaLogSetup();
  ArduinoOTA.begin();

  //MQTT
  mqttStart();

  //NTP
  corentp::setOffsetFromSettings(tzOffsetSeconds(settings::getInt(keys::dev::timezone)));

  // Ждать синхронизации на загрузке незачем: первый же tick() отправит запрос,
  // а ответ подберётся, когда придёт. Расписание таймеров до этого момента
  // держится на isTimeSet() и не сработает по случайному времени.

  portalStart();

  // Куча после загрузки -- точка отсчёта для всего последующего: сравнив её
  // со свежим баннером после самопроизвольной перезагрузки, видно, утекала
  // память или устройство упало по другой причине.
  LOG_I(boot, String(F("ready heap=")) + ESP.getFreeHeap());
}


void factoryReset(){
  LOG_W(set, F("factory reset"));
  // Прежде чем терять имя, снять с брокера всё, что под ним опубликовано:
  // после сброса топики будут другими, и старые retained-сообщения остались бы
  // у брокера навсегда -- вместе с сущностью в HomeAssistant, которая никогда
  // не оживёт.
  mqttClearRetained();
  settings::clear();
  // Тоже через заказ: сброс приходит из обработчика формы. Пока перезагрузка
  // ждёт своего срока, публикации в MQTT остановлены -- иначе очередное
  // периодическое сообщение вернуло бы брокеру только что снятый топик.
  restartRequest("factory reset");
}


// Заказать перезагрузку. Зовётся из обработчиков формы и клика, а те работают
// ДО отправки ответа (portal.h:221), поэтому перезагружаться прямо здесь
// нельзя: ответ уйти не успеет и страница повиснет. Разбор в
// restart_request.h.
//
// Причина -- обязательный аргумент, а не удобство: перезагрузок у устройства
// пять разных поводов, и в логе они выглядели одинаково. После неё в
// следующем баннере стоит reset=Software/Restart, и связка "почему заказали"
// плюс "чем закончилось" читается через перезагрузку.
void restartRequest(const char* reason){
  LOG_I(boot, String(F("reboot requested reason=")) + reason);
  restartPending.request(millis());
}


// Наступил ли срок. Зовётся из loop(), то есть заведомо после того, как
// portal.tick() отдал ответ браузеру.
void restartTick(){
  if (restartPending.tick(millis())) restart();
}


void restart(){
  LOG_I(boot, F("rebooting"));
  SendAvailableMessage("offline");
  mqttClient.loop();
  portal.tick();
  settings::commit();
  ESP.restart();
}
