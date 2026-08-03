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


// Опрос времени.
//
// Две беды NTPClient, обе вылезли на живом устройстве.
//
// Первая: неудачные попытки он не разводит во времени. _lastUpdate ставится
// ТОЛЬКО при успехе (NTPClient.cpp:105), а update() пропускает проверку
// интервала, пока _lastUpdate равен нулю (NTPClient.cpp:121). Голый
// timeClient.update() в loop() до первой синхронизации срабатывал на каждом
// проходе. Отсюда свой таймер.
//
// Вторая: beginPacket(name, port) резолвит имя на КАЖДУЮ попытку
// (NTPClient.cpp:200), а неудачный резолв блокирует loop() на таймаут DNS --
// десятки секунд. Пока время не синхронизировано, портал в эти окна
// недоступен, то есть ровно тогда, когда устройство настраивают. Поэтому имя
// резолвится один раз, а клиенту отдаётся точечная запись адреса: её lwIP
// разбирает сам, не ходя в сеть.

const char* NTP_POOL = "pool.ntp.org";

// Период повторных попыток. Первая делается на загрузке, дальше по нему.
static const uint32_t NTP_RETRY_MS = 60000;

// Резолв делается с коротким таймаутом: в худшем случае это цена одной
// попытки в минуту, а не десятки секунд.
static const uint32_t NTP_RESOLVE_TIMEOUT_MS = 2000;

// Отрезолвленный адрес мог оказаться нерабочим -- пул отдаёт разные машины.
// После стольких неудач подряд резолвим заново.
static const uint8_t NTP_RERESOLVE_AFTER = 10;


// Строка обязана пережить вызов: setPoolServerName запоминает указатель.
String ntpServerAddr;
uint8_t ntpFails = 0;


bool ntpResolve(){
  IPAddress ip;
  if (!WiFi.hostByName(NTP_POOL, ip, NTP_RESOLVE_TIMEOUT_MS)) return false;

  ntpServerAddr = ip.toString();
  timeClient.setPoolServerName(ntpServerAddr.c_str());
  println("NTP server resolved: " + ntpServerAddr);
  return true;
}


void ntpSync(){
  // Без сети попытка гарантированно упрётся в таймаут, а это время впустую.
  if (WiFi.status() != WL_CONNECTED) return;

  if (!ntpServerAddr.length() && !ntpResolve()) return;

  if (timeClient.update()){
    ntpFails = 0;
    return;
  }

  if (++ntpFails >= NTP_RERESOLVE_AFTER){
    ntpFails = 0;
    ntpServerAddr = "";  // следующая попытка начнётся с резолва
  }
}


void ntpTick(){
  if (!NtpTimer.tick()) return;
  ntpSync();
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
  timeClient.setTimeOffset(tzOffsetSeconds(settings::getInt(keys::dev::timezone)));
  timeClient.begin();

  // Одна попытка на загрузке, дальше по таймеру. Неудача ничему не мешает:
  // расписание таймеров всё равно ждёт isTimeSet().
  NtpTimer.setTime(NTP_RETRY_MS);
  NtpTimer.start();
  ntpSync();

  portalStart();

  println("Boot complete");
  println("-------------------------------");
}


void factoryReset(){
  println("Factory reset");
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
