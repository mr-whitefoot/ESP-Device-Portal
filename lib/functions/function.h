void println(const String& text){
  Serial.println(text);
  glog.println(text);
}


void print(const String& text){
  Serial.print(text);
  glog.print(text);
}


TimerScheduler timerScheduler;


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

  uint32_t due = timerScheduler.due(data.timers, now);
  if(!due) return;

  for(uint8_t i=0; i<TIMER_COUNT; i++){
    if(!(due & (1UL << i))) continue;

    println("Timer "+String(i)+" activating");
    switch(data.timers.timer[i].action){
      case TIMER_ACTION_ON:     Relay1.SetState(true);  break;
      case TIMER_ACTION_OFF:    Relay1.SetState(false); break;
      case TIMER_ACTION_TOGGLE: Relay1.ResetState();    break;
    }
  }
}


void readConfig(){
  data.deviceName = db[keys::deviceName].toString();
  data.relayInvertMode = db[keys::relayInvertMode];
  data.saveRelayStatus = db[keys::saveRelayStatus];
  data.relayState = db[keys::relayState];
  data.timezone = db[keys::timezone];

  data.wifiSsid = db[wifi::ssid].toString();
  data.wifiPass = db[wifi::password].toString();

  data.mqttServerIp = db[mqtt::serverIp].toString();
  data.mqttServerPort = db[mqtt::serverPort];
  data.mqttUsername = db[mqtt::username].toString();
  data.mqttPassword = db[mqtt::password1].toString();
  data.mqttStatusDelay = db[mqtt::status_delay];
  data.mqttAvaibleDelay = db[mqtt::avaible_delay];
  data.mqttTopicPrefix = db[mqtt::topicPrefix].toString();

  db[keys::timer].writeTo(data.timers);
}


void updateConfig(){
  db[keys::deviceName] = data.deviceName;
  db[keys::relayInvertMode] = data.relayInvertMode;
  db[keys::saveRelayStatus] = data.saveRelayStatus;
  db[keys::relayState] = data.relayState;
  db[keys::timezone] = data.timezone;

  db[wifi::ssid] = data.wifiSsid;
  db[wifi::password] = data.wifiPass;

  db[mqtt::serverIp] = data.mqttServerIp;
  db[mqtt::serverPort] = data.mqttServerPort;
  db[mqtt::username] = data.mqttUsername;
  db[mqtt::password1] = data.mqttPassword;
  db[mqtt::status_delay] = data.mqttStatusDelay;
  db[mqtt::avaible_delay] = data.mqttAvaibleDelay;
  db[mqtt::topicPrefix] = data.mqttTopicPrefix;

  db[keys::timer] = data.timers;

  db.update();
}


void portalStart(){
  println("Starting portal");
  portal.attachBuild(portalBuild);
  portal.disableAuth();
  portal.attach(portalAction);
  portal.OTA.attachUpdateBuild(OTAbuild);
  portal.start(data.deviceName.c_str());
  portal.enableOTA();
}


void timerRead(){
  db[keys::timer].writeTo(data.timers);
}


void dbSetup(){
  Serial.println("-----------------------------");
  Serial.println("Initialize database:");
  LittleFS.begin();

  if (!db.begin()){
    Serial.println("Database initialize error"); };

  db.init(keys::deviceName, "ESP Relay");
  db.init(keys::relayInvertMode, false);
  db.init(keys::saveRelayStatus, false);
  db.init(keys::relayState, false);
  db.init(keys::timezone, TIMEZONE_UTC);

  db.init(mqtt::topicPrefix, "homeassistant");
  db.init(mqtt::serverPort, 1883 );
  db.init(mqtt::status_delay, 10);
  db.init(mqtt::avaible_delay, 60);

  // forceAP инициализирует сама wifi-библиотека в wifiSetup(): ключ должен
  // иметь ровно одного владельца.
  db.init(wifi::ssid, "");
  db.init(wifi::password, "");

  readConfig();

  #ifdef DEBUG_DB
    db.dump(Serial);
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
  
  //Database
  dbSetup();
  
  //Relay
  println("Initialize relay");
  Relay1.begin(RELAY_PIN, data.relayInvertMode);
  Relay1.ChangeStateCallback(ChangeRelayState);
  if(data.saveRelayStatus){ 
      println("Restore relay state");
      Relay1.SetState(data.relayState); };

  // WiFi
  wifiSetup(data.deviceName, &db);
  
  // Enable OTA update
  println("Starting OTA updates");
  ArduinoOTA.begin();

  //MQTT
  mqttStart();

  //NTP 
  println("Starting NTP");
  timeClient.setPoolServerName("pool.ntp.org");
  timeClient.setTimeOffset(tzOffsetSeconds(data.timezone));
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
  db.clear();
  db.update();
  readConfig();
  restart();
}


void restart(){
  println("Rebooting...");
  println("-------------------------------");
  SendAvailableMessage("offline");
  mqttClient.loop();
  portal.tick();
  updateConfig();
  ESP.restart();
}


void ChangeRelayState(){
  println("Change relay state triggered");
  if(data.saveRelayStatus){
    println("Save relay state");
    data.relayState = Relay1.GetState();
    updateConfig();
  }
  publishRelay();
}
