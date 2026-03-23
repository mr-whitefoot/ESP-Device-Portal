void println(const String& text){
  Serial.println(text);
  glog.println(text);
}


void print(const String& text){
  Serial.print(text);
  glog.print(text);
}


int convertTimezoneToOffset(byte timezone){
  if(timezone == 1 ) return -43200; //-12:00 
  if(timezone == 2 ) return -39600; //-11:00
  if(timezone == 3 ) return -36000; //-10:00
  if(timezone == 4 ) return -34200; //-09:30
  if(timezone == 5 ) return -32400; //-09:00
  if(timezone == 6 ) return -28800; //-08:00
  if(timezone == 7 ) return -25200; //-07:00
  if(timezone == 8 ) return -21600; //-06:00
  if(timezone == 9 ) return -18000; //-05:00
  if(timezone == 10 ) return -14400; //-04:00
  if(timezone == 11 ) return -12600; //-03:30
  if(timezone == 12 ) return -10800; //-03:00
  if(timezone == 13 ) return -7200; //-02:00
  if(timezone == 14 ) return -3600; //-01:00
  if(timezone == 15 ) return 0;     //UTC
  if(timezone == 16 ) return 3600;  //+01:00
  if(timezone == 17 ) return 7200;  //+02:00
  if(timezone == 18 ) return 10800; //+03:00
  if(timezone == 19 ) return 12600; //+03:30
  if(timezone == 19 ) return 14400; //+04:00
  if(timezone == 20 ) return 16200; //+04:30
  if(timezone == 21 ) return 18000; //+05:00
  if(timezone == 22 ) return 19800; //+05:30
  if(timezone == 23 ) return 20700; //+05:45
  if(timezone == 24 ) return 21600; //+06:00
  if(timezone == 25 ) return 23400; //+06:30
  if(timezone == 26 ) return 25200; //+07:00
  if(timezone == 27 ) return 28800; //+08:00
  if(timezone == 28 ) return 31500; //+08:45
  if(timezone == 29 ) return 32400; //+09:00
  if(timezone == 30 ) return 34200; //+09:30
  if(timezone == 31 ) return 36000; //+10:00
  if(timezone == 32 ) return 37800; //+10:30
  if(timezone == 33 ) return 39600; //+11:00
  if(timezone == 34 ) return 43200; //+12:00
  if(timezone == 35 ) return 46800; //+13:00
  if(timezone == 36 ) return 50400; //+14:00

  return 0;
}


void timerHandle(){
  int hours   = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();

  for(int i=0; i<TIMER_COUNT; i++){
    if( data.timers.timer[i].enable  == true &&
        data.timers.timer[i].hours   == hours &&
        data.timers.timer[i].minutes == minutes &&
        data.timers.timer[i].seconds == seconds)
      {  
        println("Timer "+String(i)+" activating");
        if(data.timers.timer[i].action == 0){Relay1.SetState(true);}
        if(data.timers.timer[i].action == 1){Relay1.SetState(false);}
        if(data.timers.timer[i].action == 2){Relay1.ResetState();}
      }
  }
}


void readConfig(){
  data.deviceName = db[keys::deviceName].toString();
  data.relayInvertMode = db[keys::relayInvertMode];
  data.saveRelayStatus = db[keys::saveRelayStatus];
  data.relayState = db[keys::relayState];
  data.theme = db[keys::theme];
  data.timezone = db[keys::timezone];

  data.wifiSsid = db[wifi::ssid].toString();
  data.wifiPass = db[wifi::password].toString();
  data.wifiForceAP = db[wifi::forceAP];

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
  db[keys::theme] = data.theme;
  db[keys::timezone] = data.timezone;

  db[wifi::ssid] = data.wifiSsid;
  db[wifi::password] = data.wifiPass;
  db[wifi::forceAP] = data.wifiForceAP;

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
  db.init(keys::theme, LIGHT_THEME);
  db.init(keys::timezone, 14);

  db.init(mqtt::topicPrefix, "homeassistant");
  db.init(mqtt::serverPort, 1883 );
  db.init(mqtt::status_delay, 10);
  db.init(mqtt::avaible_delay, 60);

  db.init(wifi::ssid, "");
  db.init(wifi::password, "");
  db.init(wifi::forceAP, true);

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
  Relay1.SetPin(RELAY_PIN);
  Relay1.SetInvertMode(data.relayInvertMode);
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
  timeClient.setTimeOffset(convertTimezoneToOffset(data.timezone));
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
