void println(const String& text){
  Serial.println(text);
  glog.println(text);
}


void print(const String& text){
  Serial.print(text);
  glog.print(text);
}


void portalStart(){
  println("Starting portal");
  portal.attachBuild(portalBuild);
  portal.disableAuth();
  portal.attach(portalAction);
  portal.OTA.attachUpdateBuild(OTAbuild);
  // Имя берётся из corewifi: portal.start() запоминает сырой указатель, и
  // строка обязана пережить вызов. Временный String из getStringValue умирал
  // в конце выражения.
  portal.start(corewifi::portalName.c_str());
  portal.enableOTA();
  corewifi::portalStarted();
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
static const int32_t SETTINGS_SCHEMA = 1;


void settingsMigrate(){
  int32_t schema = settings::getInt(keys::sys::schema);
  if(schema == SETTINGS_SCHEMA) return;

  if(schema == 0){
    // Либо чистое устройство, либо прошивка до 3.1.0, где ключи назывались
    // иначе. Во втором случае старые ячейки всё равно недостижимы, и оставлять
    // их значит вечно носить около трёхсот байт мусора в файле базы.
    println("Settings schema 0: starting fresh");
    settings::clear();
  }

  settings::setInt(keys::sys::schema, SETTINGS_SCHEMA);
  settings::commit();
}


void settingsSetup(){
  Serial.println("-----------------------------");
  Serial.println("Initialize settings:");

  if (!settings::begin()){
    Serial.println("Settings initialize error"); };

  // Миграция до значений по умолчанию: она может очистить базу, и defineX
  // ниже наполнят её заново.
  settingsMigrate();

  // define создаёт параметр, только если его ещё нет, и никогда не трогает
  // сохранённое. Именно поэтому новый параметр в следующей прошивке получит
  // значение по умолчанию, а остальные переживут обновление.
  settings::defineString(keys::dev::name, (String("ESP ") + device::model()).c_str());
  settings::defineInt(keys::dev::timezone, TIMEZONE_UTC);

  settings::defineString(keys::mqtt::topicPrefix, "homeassistant");
  // Пусто -- значит за переименованием убирать нечего.
  settings::defineString(keys::mqtt::prevName, "");
  settings::defineInt(keys::mqtt::port, 1883);
  settings::defineInt(keys::mqtt::statusDelay, 10);
  settings::defineInt(keys::mqtt::availableDelay, 60);

  device::defineSettings();

  settings::defineString(keys::wifi::ssid, "");
  settings::defineString(keys::wifi::password, "");

  #ifdef DEBUG_DB
    settings::detail::db().dump(Serial);
    println(" ");
  #endif
}


void startup(){
  Serial.begin(74880);
  //Log
  glog.start(1000);

  println("");println("");println("");
  println("-------------------------------");
  println("Booting...");

  //Settings
  settingsSetup();

  //Device
  println("Initialize device");
  device::begin();

  // WiFi
  corewifi::begin(settings::getStringValue(keys::dev::name));

  // Enable OTA update
  println("Starting OTA updates");
  ArduinoOTA.begin();

  //MQTT
  mqttStart();

  //NTP
  println("Starting NTP");
  corentp::setOffsetFromSettings(tzOffsetSeconds(settings::getInt(keys::dev::timezone)));

  // Ждать синхронизации на загрузке незачем: первый же tick() отправит запрос,
  // а ответ подберётся, когда придёт. Расписание таймеров до этого момента
  // держится на isTimeSet() и не сработает по случайному времени.

  portalStart();

  println("Boot complete");
  println("-------------------------------");
}


void factoryReset(){
  println("Factory reset");
  // Прежде чем терять имя, снять с брокера всё, что под ним опубликовано:
  // после сброса топики будут другими, и старые retained-сообщения остались бы
  // у брокера навсегда -- вместе с сущностью в HomeAssistant, которая никогда
  // не оживёт.
  mqttClearRetained();
  settings::clear();
  restart();
}


void restart(){
  println("Rebooting...");
  println("-------------------------------");
  SendAvailableMessage("offline");
  mqttClient.loop();
  portal.tick();
  settings::commit();
  ESP.restart();
}
