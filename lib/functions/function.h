void println(const String& text){
  Serial.println(text);
  glog.println(text);
}


void print(const String& text){
  Serial.print(text);
  glog.print(text);
}


TimerScheduler timerScheduler;


void timersLoad(){
  for(uint8_t i=0; i<TIMER_COUNT; i++){
    timers.timer[i].enable  = settings::getBool(keys::timer::enable[i]);
    timers.timer[i].action  = (uint8_t)settings::getInt(keys::timer::action[i]);
    timers.timer[i].hours   = (uint8_t)settings::getInt(keys::timer::hours[i]);
    timers.timer[i].minutes = (uint8_t)settings::getInt(keys::timer::minutes[i]);
    timers.timer[i].seconds = (uint8_t)settings::getInt(keys::timer::seconds[i]);
  }
}


void timerHandle(){
  // До первой синхронизации NTPClient отсчитывает время от нуля, то есть
  // отдаёт 00:00:xx: таймер на начало суток срабатывал бы сразу после
  // включения. Заодно сбрасываем точку отсчёта, чтобы момент синхронизации
  // не выглядел скачком часов.
  if(!timeClient.isTimeSet()){
    timerScheduler.resync();
    return;
  }

  uint32_t now = (uint32_t)timeClient.getHours() * 3600UL +
                 (uint32_t)timeClient.getMinutes() * 60UL +
                 (uint32_t)timeClient.getSeconds();

  uint32_t due = timerScheduler.due(timers, now);
  if(!due) return;

  for(uint8_t i=0; i<TIMER_COUNT; i++){
    if(!(due & (1UL << i))) continue;

    println("Timer "+String(i)+" activating");
    switch(timers.timer[i].action){
      case TIMER_ACTION_ON:     Relay1.SetState(true);  break;
      case TIMER_ACTION_OFF:    Relay1.SetState(false); break;
      case TIMER_ACTION_TOGGLE: Relay1.ResetState();    break;
    }
  }
}


void portalStart(){
  println("Starting portal");
  portal.attachBuild(portalBuild);
  portal.disableAuth();
  portal.attach(portalAction);
  portal.OTA.attachUpdateBuild(OTAbuild);
  portal.start(settings::getStringValue(keys::dev::name).c_str());
  portal.enableOTA();
}


void settingsSetup(){
  Serial.println("-----------------------------");
  Serial.println("Initialize settings:");

  if (!settings::begin()){
    Serial.println("Settings initialize error"); };

  // define создаёт параметр, только если его ещё нет, и никогда не трогает
  // сохранённое. Именно поэтому новый параметр в следующей прошивке получит
  // значение по умолчанию, а остальные переживут обновление.
  settings::defineString(keys::dev::name, "ESP Relay");
  settings::defineInt(keys::dev::timezone, TIMEZONE_UTC);

  settings::defineBool(keys::relay::invert, false);
  settings::defineBool(keys::relay::saveState, false);
  settings::defineBool(keys::relay::state, false);

  settings::defineString(keys::mqtt::topicPrefix, "homeassistant");
  settings::defineInt(keys::mqtt::port, 1883);
  settings::defineInt(keys::mqtt::statusDelay, 10);
  settings::defineInt(keys::mqtt::availableDelay, 60);

  for(uint8_t i=0; i<TIMER_COUNT; i++){
    settings::defineBool(keys::timer::enable[i], false);
    settings::defineInt(keys::timer::action[i], TIMER_ACTION_ON);
    settings::defineInt(keys::timer::hours[i], 0);
    settings::defineInt(keys::timer::minutes[i], 0);
    settings::defineInt(keys::timer::seconds[i], 0);
  }

  // forceAP инициализирует сама wifi-библиотека в wifiSetup(): ключ должен
  // иметь ровно одного владельца.
  settings::defineString(keys::wifi::ssid, "");
  settings::defineString(keys::wifi::password, "");

  timersLoad();

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

  //Relay
  println("Initialize relay");
  Relay1.begin(RELAY_PIN, settings::getBool(keys::relay::invert));
  Relay1.ChangeStateCallback(ChangeRelayState);
  if(settings::getBool(keys::relay::saveState)){
      println("Restore relay state");
      Relay1.SetState(settings::getBool(keys::relay::state)); };

  // WiFi
  // Библиотека работает с базой напрямую, поэтому получает экземпляр из
  // слоя. Уйдёт вместе с переходом на DBConnector.
  wifiSetup(settings::getStringValue(keys::dev::name), &settings::detail::db());

  // Enable OTA update
  println("Starting OTA updates");
  ArduinoOTA.begin();

  //MQTT
  mqttStart();

  //NTP
  println("Starting NTP");
  timeClient.setPoolServerName("pool.ntp.org");
  timeClient.setTimeOffset(tzOffsetSeconds(settings::getInt(keys::dev::timezone)));
  timeClient.begin();

  // Timers handler
  println("Starting timers handler");
  handleTimerDelay.setTime(1000);
  handleTimerDelay.attach(timerHandle);
  handleTimerDelay.start();

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


void ChangeRelayState(){
  println("Change relay state triggered");
  if(settings::getBool(keys::relay::saveState)){
    // Без commit: запись на флеш откладывается до settings::tick(). Иначе
    // автоматизация, дёргающая реле раз в несколько секунд, переписывала бы
    // файл базы на каждое переключение.
    settings::setBool(keys::relay::state, Relay1.GetState());
  }
  publishRelay();
}
