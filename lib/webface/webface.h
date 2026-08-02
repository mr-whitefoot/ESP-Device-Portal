// Список для GP.SELECT собирается из TIMEZONES, чтобы подписи в UI и смещения
// для NTP приходили из одного места и не могли разойтись.
String timezoneOptions(){
  String list;
  list.reserve(TIMEZONE_COUNT * 7);
  for(uint8_t i = 0; i < TIMEZONE_COUNT; i++){
    if(i) list += ',';
    list += TIMEZONES[i].label;
  }
  return list;
}


void createTimerUi(const int index){
  GP.BLOCK_TAB_BEGIN("Timer");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Timer"); GP.SWITCH("timerEnable"+String(index), timers.timer[index].enable);
    GP.BOX_END();
    GP.SELECT("timerHours"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23", timers.timer[index].hours);
    GP.SELECT("timerMinutes"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",timers.timer[index].minutes);
    GP.SELECT("timerSeconds"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",timers.timer[index].seconds);
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Action"); GP.SELECT("timerAction"+String(index), "On,Off,Toggle", timers.timer[index].action);
    GP.BOX_END();
  GP.BLOCK_END();
}


void saveTimer(const int index){
  bool enable = false;
  int action = 0, hours = 0, minutes = 0, seconds = 0;

  portal.copyBool("timerEnable"+String(index), enable);
  portal.copyInt("timerAction"+String(index), action);
  portal.copyInt("timerHours"+String(index), hours);
  portal.copyInt("timerMinutes"+String(index), minutes);
  portal.copyInt("timerSeconds"+String(index), seconds);

  settings::setBool(keys::timer::enable[index], enable);
  settings::setInt(keys::timer::action[index], action);
  settings::setInt(keys::timer::hours[index], hours);
  settings::setInt(keys::timer::minutes[index], minutes);
  settings::setInt(keys::timer::seconds[index], seconds);
}


void portalBuild(){
  uint32_t timeleftAP = WiFiApTimer.timeLeft()/1000;

  GP.BUILD_BEGIN();
  // Тема одна. Выбор из двух заставлял линкер тянуть обе таблицы стилей:
  // GP_DARK 10267 байт плюс GP_LIGHT 9654, при запасе под OTA около 20 КБ.
  GP.THEME(GP_DARK);

  // Update components
  GP.UPDATE("signal,switch,mqttStatusLed,ipAddress,wifiAPTimer,time");


  // Configuration page
  if (portal.uri() == form.config) {
    GP.PAGE_TITLE("Configuration");
    GP.TITLE("Configuration");
    GP.HR();
    GP.BUTTON_LINK(form.preferences, "Preferences");
    GP.BUTTON_LINK(form.WiFiConfig, "WiFi configuration");
    GP.BUTTON_LINK(form.mqttConfig, "MQTT configuration");
    GP.BUTTON_LINK(form.factoryReset, "Factory reset");
    GP.BUTTON_LINK(form.firmwareUpgrade, "Firmware upgrade");
    GP.BUTTON("rebootButton", "Reboot");
    GP.HR();
    GP.BUTTON_LINK(form.root, "Back");

  //Log
  } else if (portal.uri() == form.log){
    GP.PAGE_TITLE("Log");
    GP.AREA_LOG(glog, 20);
    GP.BUTTON_LINK(form.root, "Back");

  //Timers
  } else if (portal.uri() == form.timers){
    GP.FORM_BEGIN(form.timers);
      GP.PAGE_TITLE("Timers");
      GP.TITLE("Timers");
      GP.HR();
      for(int i=0; i<TIMER_COUNT; i++){
        createTimerUi(i);
      }
      GP.HR();
      GP.SUBMIT("Save");
    GP.FORM_END();

     GP.BUTTON_LINK(form.root, "Back");

  //Preferences
  } else if (portal.uri() == form.preferences){
    GP.FORM_BEGIN(form.preferences);
      GP.PAGE_TITLE("Preferences");
      GP.TITLE("Preferences");
      GP.HR();
      GP.BLOCK_TAB_BEGIN("Device name");
        GP.TEXT("deviceName", "Device name", settings::getStringValue(keys::dev::name)); GP.BREAK();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("Settings");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Relay invert mode"); GP.SWITCH("relayInvertMode", settings::getBool(keys::relay::invert));
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Save relay status"); GP.SWITCH("relaySaveStatus", settings::getBool(keys::relay::saveState));
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Timezone"); 
          GP.SELECT("timezone", timezoneOptions(), settings::getInt(keys::dev::timezone));
        GP.BOX_END();
      GP.BLOCK_END();


      GP.BLOCK_TAB_BEGIN("Information");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Firmware version");
          GP.LABEL(sw_version);
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Release date");
          GP.LABEL(release_date);
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("MAC");
          GP.LABEL(WiFi.macAddress());
        GP.BOX_END();
      GP.BLOCK_END();

      GP.HR();
      GP.SUBMIT("Save");
    GP.FORM_END();
    GP.BUTTON_LINK(form.config, "Back");


    // WiFi configuration page
  } else if (portal.uri() == form.WiFiConfig) {
      GP.FORM_BEGIN(form.WiFiConfig);
        GP.PAGE_TITLE("WiFi configuration");
        GP.TITLE("WiFi");
        GP.HR();

        GP.BLOCK_TAB_BEGIN("Information");
          if (WiFi.status() == WL_CONNECTED){
            GP.BOX_BEGIN(GP_EDGES);
              GP.LABEL("WiFi status"); GP.LED_GREEN("WiFiLed", true);
            GP.BOX_END();
            GP.BOX_BEGIN(GP_EDGES);
              GP.LABEL("Signal"); GP.LABEL("","signal");
            GP.BOX_END();
            GP.BOX_BEGIN(GP_EDGES);
              GP.LABEL("IP address"); GP.LABEL(WiFi.localIP().toString(),"ipAddress");
            GP.BOX_END();}
          else {
            GP.BOX_BEGIN(GP_EDGES);
              GP.LABEL("WiFi status"); GP.LED_GREEN("WiFiLed", false);
            GP.BOX_END();
          }
        GP.BLOCK_END();

        GP.BLOCK_TAB_BEGIN("Settings");
          GP.TEXT("ssid", "SSID", settings::getStringValue(keys::wifi::ssid));GP.BREAK();
          GP.PASS("pass", "Password", settings::getStringValue(keys::wifi::password));GP.BREAK();
        GP.BLOCK_END();

        GP.HR();
        GP.SUBMIT("Save");
        GP.BUTTON_LINK(form.config, "Back");
      GP.FORM_END();

    // MQTT configuration page
  } else if (portal.uri() == form.mqttConfig) {
    GP.FORM_BEGIN(form.mqttConfig);
      GP.PAGE_TITLE("MQTT configuration");
      GP.TITLE("MQTT");
      GP.HR();

      GP.BLOCK_TAB_BEGIN("Information");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Status"); GP.LED_GREEN("mqttStatusLed", mqttClient.isConnected());
        GP.BOX_END();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("Server");
        GP.TEXT("mqttServerIp", "Server", settings::getStringValue(keys::mqtt::host)); GP.BREAK();
        GP.NUMBER("mqttServerPort", "Port", settings::getInt(keys::mqtt::port)); GP.BREAK();
        GP.TEXT("mqttUsername", "Username", settings::getStringValue(keys::mqtt::username)); GP.BREAK();
        GP.PASS("mqttPassword", "Password", settings::getStringValue(keys::mqtt::password)); GP.BREAK();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("MQTT Message periods");
        GP.NUMBER("avaible_delay", "Avaible", settings::getInt(keys::mqtt::availableDelay)); GP.BREAK();
        GP.NUMBER("status_delay", "Message", settings::getInt(keys::mqtt::statusDelay)); GP.BREAK();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("MQTT topics");
        GP.LABEL("Topic prefix"); GP.BREAK();
        GP.TEXT("topicPrefix", "Topic prefix", settings::getStringValue(keys::mqtt::topicPrefix)); GP.BREAK();
      GP.BLOCK_END();

      GP.HR();
      GP.SUBMIT("Save and reboot");
      GP.BUTTON_LINK(form.config, "Back");;
    GP.FORM_END();

    //Factory reset page
  } else if (portal.uri() == form.factoryReset) {
    GP.FORM_BEGIN(form.factoryReset);
      GP.PAGE_TITLE("Factory reset");
      GP.TITLE("Factory reset");
      GP.HR();
      GP.BOX_BEGIN(GP_EDGES);
        GP.LABEL("I'm really understand what I do");
        GP.CHECK("resetAllow");  GP.BREAK();
      GP.BOX_END();

      GP.HR();
      GP.SUBMIT("Factory reset");
      GP.BUTTON_LINK(form.config, "Back");;
    GP.FORM_END();

    // Root page, "/"
  } else {
    GP.PAGE_TITLE("Portal");
    GP.FORM_BEGIN(form.root);
       GP.BLOCK_TAB_BEGIN("Control");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL( settings::getStringValue(keys::dev::name) ); GP.SWITCH("switch", Relay1.GetState());
        GP.BOX_END();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("WiFi");
        if (WiFi.status() == WL_CONNECTED){
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Status");GP.LED_GREEN("WiFiLed", true);
          GP.BOX_END();
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Signal"); GP.LABEL("","signal");
          GP.BOX_END();
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("IP address"); GP.LABEL(WiFi.localIP().toString(),"ipAddress");
          GP.BOX_END();
        }else{
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Status");GP.LED_GREEN("WiFiLed", false);
          GP.BOX_END();
        }
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("MQTT");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Status"); GP.LED_GREEN("mqttStatusLed", mqttClient.isConnected());
        GP.BOX_END();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("Information");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Time"); 
          GP.LABEL("","time");
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Firmware version");
          GP.LABEL(sw_version);
        GP.BOX_END();
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Release date");
          GP.LABEL(release_date);
        GP.BOX_END();
        if (WiFiApTimer.active()){
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Restart in");
            GP.LABEL(String(timeleftAP),"wifiAPTimer");
          GP.BOX_END();
        };
      GP.BLOCK_END();
      GP.HR();
      GP.BUTTON_LINK(form.timers, "Timers");
      GP.BUTTON_LINK(form.config, "Configuration");
      GP.BUTTON_LINK(form.log, "Log");
    GP.FORM_END();
  }
  GP.BUILD_END();
}


void portalCheckForm(){
  if (portal.form()) {
    //WiFi config
    if (portal.form(form.WiFiConfig)) {
      settings::setString(keys::wifi::ssid, portal.getString("ssid").c_str());
      settings::setString(keys::wifi::password, portal.getString("pass").c_str());
      settings::setBool(keys::wifi::forceAP, false);
      restart();

    // Factory reset
    } else if(portal.form(form.factoryReset)){
      Serial.println("Factory reset");
      if(portal.getCheck("resetAllow"))
        factoryReset();

    // Preferences
    } else if(portal.form(form.preferences)){
      settings::setString(keys::dev::name, portal.getString("deviceName").c_str());

      bool invert = portal.getCheck("relayInvertMode");
      settings::setBool(keys::relay::invert, invert);
      Relay1.SetInvertMode(invert);

      settings::setBool(keys::relay::saveState, portal.getCheck("relaySaveStatus"));

      int32_t timezone = portal.getInt("timezone");
      settings::setInt(keys::dev::timezone, timezone);
      timeClient.setTimeOffset(tzOffsetSeconds(timezone));

      settings::commit();

      //MQTT Config
    } else if(portal.form(form.mqttConfig)){
      settings::setString(keys::mqtt::host, portal.getString("mqttServerIp").c_str());
      settings::setInt(keys::mqtt::port, portal.getInt("mqttServerPort"));
      settings::setString(keys::mqtt::username, portal.getString("mqttUsername").c_str());
      settings::setString(keys::mqtt::password, portal.getString("mqttPassword").c_str());
      settings::setInt(keys::mqtt::availableDelay, portal.getInt("avaible_delay"));
      settings::setInt(keys::mqtt::statusDelay, portal.getInt("status_delay"));
      settings::setString(keys::mqtt::topicPrefix, portal.getString("topicPrefix").c_str());

      restart();

      //Timers
    } else if(portal.form(form.timers)){
        for(int i=0; i < TIMER_COUNT; i++){ saveTimer(i); };
        settings::commit();
        timersLoad();   // обновить кэш расписания
    }
  }

  if (portal.update()){
    long rssi = WiFi.RSSI();
    int strength = map(rssi, -80, -20, 0, 100);
    String wifiStrength = String(strength)+"%";
    portal.updateString("signal", wifiStrength);

    portal.updateInt("switch", Relay1.GetState());
    portal.updateInt("mqttStatusLed",mqttClient.isConnected());
    String ipAdress = WiFi.localIP().toString();
    portal.updateString("ipAddress", ipAdress);

    uint32_t timeleftAP = WiFiApTimer.timeLeft()/1000;
    portal.updateInt("wifiAPTimer", timeleftAP);

    String time = timeClient.getFormattedTime();
    portal.updateString("time", time);

    portal.updateLog(glog);
  }
}


void portalAction(){
  portalCheckForm();

  if (portal.click()){
    Serial.println("Portal click");

    // Переключатели инверсии и сохранения состояния разбираются в обработчике
    // формы Preferences по кнопке Save, как и остальные её поля. Здесь были
    // отдельные обработчики кликов, причём один из них с опечаткой в имени
    // ("relayInverMode") и потому мёртвый, а второй сохранял настройку в обход
    // кнопки Save -- поведение расходилось между двумя переключателями.
    if (portal.click("switch")){ Relay1.SetState( portal.getCheck("switch") ); }
    if (portal.click("rebootButton")){ restart(); }
  }
}


//Custom OTA page
void OTAbuild(bool UpdateEnd, const String& UpdateError) {
  GP.BUILD_BEGIN(400);
    GP.THEME(GP_DARK);
    GP.PAGE_TITLE(F("Firmware upgrade"));
    if (!UpdateEnd) {
      GP.BLOCK_TAB_BEGIN(F("Firmware upgrade"));
        GP.OTA_FIRMWARE(F("OTA firmware"), GP_GREEN, true);
      GP.BLOCK_END();
      GP.BUTTON_LINK(form.config, "Back");
    } else if (UpdateError.length()) {
      GP.BLOCK_TAB_BEGIN(F("Firmware upgrade"));
        GP.TITLE(String(F("Update error: ")) + UpdateError);
        GP.BUTTON_LINK(form.firmwareUpgrade, F("Refresh"));
      GP.BLOCK_END();

    } else {
      GP.BLOCK_TAB_BEGIN(F("Firmware upgrade"));
        GP.TITLE(F("Update Success!"));
        GP.TITLE(F("Rebooting..."));
      GP.BLOCK_END();
      GP.BUTTON_LINK(form.root, "Home");
    }
  GP.BUILD_END();
}
