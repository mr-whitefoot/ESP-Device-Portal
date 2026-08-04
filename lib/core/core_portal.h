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


// Сохранённый пароль в форму не подставляется. GP.PASS рисует
// <input type='password'>, но значение кладёт в атрибут value как есть:
// маскировка работает только для глаз, а `curl` страницы отдаёт пароль
// открытым текстом кому угодно в сети. Поэтому поле всегда пустое, а то,
// что пароль всё-таки сохранён, видно по подсказке внутри поля.
const char* passwordPlaceholder(bool saved){
  return saved ? "Saved, empty keeps it" : "Password";
}


// Tasmota на странице настройки WiFi показывает три самые сильные сети,
// убирает повторяющиеся SSID и по клику переносит имя сети в форму. Здесь тот
// же сценарий, но scan остаётся асинхронным: блокировать loop() на несколько
// секунд особенно опасно для captive portal и OneWire на ESP8266.
constexpr uint8_t WIFI_SCAN_PREVIEW_COUNT = 3;
constexpr uint8_t WIFI_SCAN_RESULT_LIMIT = 16;

String wifiScanEscapeHtml(const String& value){
  String escaped;
  escaped.reserve(value.length() + 8);
  for(size_t i = 0; i < value.length(); i++){
    switch(value[i]){
      case '&':  escaped += F("&amp;");  break;
      case '<':  escaped += F("&lt;");   break;
      case '>':  escaped += F("&gt;");   break;
      case '\"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;");  break;
      default:   escaped += value[i];     break;
    }
  }
  return escaped;
}

uint8_t wifiScanQuality(int32_t rssi){
  if(rssi <= -100) return 0;
  if(rssi >= -50) return 100;
  return static_cast<uint8_t>(2 * (rssi + 100));
}

// Возвращает индексы уникальных SSID от сильного к слабому. Для одинакового
// имени оставляем лучший BSSID, как сокращённый список Tasmota. Жёсткий предел
// нужен для ESP-01: число соседских точек не должно определять расход стека.
uint8_t wifiScanSortedIndices(int16_t found, uint8_t* indices, uint8_t capacity){
  uint8_t used = 0;
  for(int16_t candidate = 0; candidate < found; candidate++){
    String candidateSsid = WiFi.SSID(candidate);
    if(!candidateSsid.length()) continue;

    int8_t position = -1;
    for(uint8_t i = 0; i < used; i++){
      if(WiFi.SSID(indices[i]) == candidateSsid){
        position = i;
        break;
      }
    }

    if(position >= 0){
      if(WiFi.RSSI(candidate) <= WiFi.RSSI(indices[position])) continue;
      indices[position] = candidate;
    } else if(used < capacity){
      position = used;
      indices[used++] = candidate;
    } else {
      if(WiFi.RSSI(candidate) <= WiFi.RSSI(indices[used - 1])) continue;
      position = used - 1;
      indices[position] = candidate;
    }

    while(position > 0 &&
          WiFi.RSSI(indices[position]) > WiFi.RSSI(indices[position - 1])){
      uint8_t previous = indices[position - 1];
      indices[position - 1] = indices[position];
      indices[position] = previous;
      position--;
    }
  }
  return used;
}

int16_t wifiScanStatus(){
  int16_t status = WiFi.scanComplete();

  // scan=1 приходит только с явной ссылки «Scan again». Сначала освобождаем
  // предыдущий результат SDK, иначе будет показан старый список.
  if(portal.hasArg("scan")){
    WiFi.scanDelete();
    status = WiFi.scanNetworks(true);
  } else if(status == WIFI_SCAN_FAILED){
    // Первый заход после загрузки: список ещё не строился.
    status = WiFi.scanNetworks(true);
  }
  return status;
}

void wifiScanReload(bool expanded){
  GP.JS_BEGIN();
  GP.SEND(F("setTimeout(function(){location.replace('"));
  GP.SEND(form.WiFiConfig);
  if(expanded) GP.SEND(F("?all=1"));
  GP.SEND(F("');},900);"));
  GP.JS_END();
}

void wifiScanBuild(){
  bool expanded = portal.hasArg("all") || portal.hasArg("scan");
  int16_t found = wifiScanStatus();

  GP.BLOCK_TAB_BEGIN("Available networks");
    if(found == WIFI_SCAN_RUNNING){
      GP.LABEL("Scanning...");
      wifiScanReload(expanded);
    } else if(found == WIFI_SCAN_FAILED){
      GP.LABEL("WiFi scan failed");
    } else if(found == 0){
      GP.LABEL("No networks found");
    } else {
      uint8_t indices[WIFI_SCAN_RESULT_LIMIT];
      uint8_t unique = wifiScanSortedIndices(found, indices, WIFI_SCAN_RESULT_LIMIT);
      uint8_t shown = expanded ? unique : min(unique, WIFI_SCAN_PREVIEW_COUNT);

      GP.SEND(F(
        "<style>"
        ".wifi-net{display:flex;align-items:center;justify-content:space-between;"
          "gap:8px;padding:8px 4px;text-decoration:none}"
        ".wifi-name{overflow-wrap:anywhere;text-align:left}"
        ".wifi-rssi{white-space:nowrap;font-size:.85em;opacity:.8}"
        ".wifi-bars{display:inline-flex;align-items:flex-end;height:14px;margin-left:5px}"
        ".wifi-bars i{display:block;width:3px;margin-left:1px;border-radius:2px;"
          "background:currentColor;opacity:.25}"
        ".wifi-bars i.on{opacity:1}"
        ".wifi-bars i:nth-child(1){height:25%}"
        ".wifi-bars i:nth-child(2){height:50%}"
        ".wifi-bars i:nth-child(3){height:75%}"
        ".wifi-bars i:nth-child(4){height:100%}"
        "</style>"));

      for(uint8_t i = 0; i < shown; i++){
        uint8_t index = indices[i];
        int32_t rssi = WiFi.RSSI(index);
        uint8_t quality = wifiScanQuality(rssi);
        uint8_t bars = (quality + 24) / 25;

        GP.SEND(F("<a class='wifi-net' href='#' onclick='wifiPick(this);return false;'>"
                  "<span class='wifi-name'>"));
        GP.SEND(wifiScanEscapeHtml(WiFi.SSID(index)));
        GP.SEND(F("</span><span class='wifi-rssi'>"));
        if(WiFi.encryptionType(index) != ENC_TYPE_NONE) GP.SEND(F("&#128274; "));
        GP.SEND(String(quality));
        GP.SEND(F("% <span class='wifi-bars'>"));
        for(uint8_t bar = 0; bar < 4; bar++)
          GP.SEND(bar < bars ? F("<i class='on'></i>") : F("<i></i>"));
        GP.SEND(F("</span></span></a>"));
      }

      GP.JS_BEGIN();
      GP.SEND(F(
        "function wifiPick(row){"
          "document.getElementById('ssid').value="
            "row.querySelector('.wifi-name').textContent;"
          "document.getElementById('pass').focus();"
        "}"));
      GP.JS_END();

      if(!expanded && unique > shown){
        GP.SEND(F("<a href='"));
        GP.SEND(form.WiFiConfig);
        GP.SEND(F("?all=1'>Show more WiFi networks</a><br>"));
      }
    }

    if(found != WIFI_SCAN_RUNNING){
      GP.SEND(F("<a href='"));
      GP.SEND(form.WiFiConfig);
      GP.SEND(F("?scan=1'>Scan again</a>"));
    }
  GP.BLOCK_END();
}


void portalBuild(){
  uint32_t retryLeft = corewifi::retryLeftSeconds();

  GP.BUILD_BEGIN();
  // Тема одна. Выбор из двух заставлял линкер тянуть обе таблицы стилей:
  // GP_DARK 10267 байт плюс GP_LIGHT 9654, при запасе под OTA около 20 КБ.
  GP.THEME(GP_DARK);

  // Ядро обновляет свои поля, устройство добавляет свои.
  {
    String ids = "signal,mqttStatusLed,ipAddress,wifiAPTimer,time";
    const char* deviceIds = device::updateIds();
    if (deviceIds && *deviceIds) { ids += ','; ids += deviceIds; }
    GP.UPDATE(ids);
  }


  // Страницы устройства идут первыми: ядро не знает, какие они.
  if (device::buildPage(portal.uri())) {
    GP.BUILD_END();
    return;

  // Configuration page
  } else if (portal.uri() == form.config) {
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

  //Preferences
  } else if (portal.uri() == form.preferences){
    GP.FORM_BEGIN(form.preferences);
      GP.PAGE_TITLE("Preferences");
      GP.TITLE("Preferences");
      GP.HR();
      GP.BLOCK_TAB_BEGIN("Device name");
        GP.TEXT("deviceName", "Device name", settings::getStringValue(keys::dev::name)); GP.BREAK();
      GP.BLOCK_END();

      device::buildSettingsUi();

      GP.BLOCK_TAB_BEGIN("Settings");
        GP.BOX_BEGIN(GP_EDGES);
          GP.LABEL("Timezone"); 
          GP.SELECT("timezone", timezoneOptions(), settings::getInt(keys::dev::timezone));
        GP.BOX_END();
      GP.BLOCK_END();

      // Учётные данные показываются только при включённой авторизации.
      // Пароль, как и остальные секреты портала, в HTML не возвращаем:
      // пустое поле при сохранении оставляет прежнее значение.
      {
        bool authEnabled = settings::getBool(keys::portal::authEnabled);
        bool passwordSaved = settings::getStringValue(keys::portal::password).length();

        GP.BLOCK_TAB_BEGIN("Authorization");
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Authorization");
            GP.CHECK("authEnabled", authEnabled);
          GP.BOX_END();

          if (authEnabled)
            GP.SEND(F("<div id='authCredentials'>\n"));
          else
            GP.SEND(F("<div id='authCredentials' style='display:none'>\n"));
          GP.TEXT("authUsername", "Username", settings::getStringValue(keys::portal::username)); GP.BREAK();
          GP.PASS("authPassword", passwordPlaceholder(passwordSaved), ""); GP.BREAK();
          GP.SEND(F("</div>\n"));
        GP.BLOCK_END();

        // required не ставится навсегда: сохранённый пароль можно оставить,
        // отправив пустое поле, а при выключенном чекбоксе скрытые поля не
        // должны мешать сохранению остальных настроек.
        GP.JS_BEGIN();
        GP.SEND(F(
          "const authEnabled=document.getElementById('authEnabled');"
          "const authCredentials=document.getElementById('authCredentials');"
          "const authUsername=document.getElementById('authUsername');"
          "const authPassword=document.getElementById('authPassword');"
          "function toggleAuthCredentials(){"
            "const enabled=authEnabled.checked;"
            "authCredentials.style.display=enabled?'block':'none';"
            "authUsername.required=enabled;"
            "authPassword.required=enabled&&(authPassword.dataset.saved!='1'||"
              "authUsername.value!==authUsername.defaultValue);"
          "}"
          "authPassword.dataset.saved='"));
        GP.SEND(passwordSaved ? F("1") : F("0"));
        GP.SEND(F(
          "';"
          "authEnabled.addEventListener('change',toggleAuthCredentials);"
          "authUsername.addEventListener('input',toggleAuthCredentials);"
          "toggleAuthCredentials();"));
        GP.JS_END();
      }


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

        wifiScanBuild();

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
          GP.PASS("pass", passwordPlaceholder(settings::getStringValue(keys::wifi::password).length()), "");GP.BREAK();
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
        GP.PASS("mqttPassword", passwordPlaceholder(settings::getStringValue(keys::mqtt::password).length()), ""); GP.BREAK();
      GP.BLOCK_END();

      GP.BLOCK_TAB_BEGIN("MQTT Message periods");
        GP.NUMBER("available_delay", "Available", settings::getInt(keys::mqtt::availableDelay)); GP.BREAK();
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
    GP.PAGE_TITLE(settings::getStringValue(keys::dev::name));
    GP.FORM_BEGIN(form.root);
      device::buildHomeUi();

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
        if (retryLeft){
          GP.BOX_BEGIN(GP_EDGES);
            GP.LABEL("Retry in");
            GP.LABEL(String(retryLeft),"wifiAPTimer");
          GP.BOX_END();
        };
      GP.BLOCK_END();
      GP.HR();
      device::buildHomeLinks();
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
      // Пустое поле пароля означает «оставить прежний»: в форму сохранённый
      // пароль не подставляется, и иначе любое сохранение страницы затирало бы
      // его пустой строкой. Но у WiFi пустой пароль -- законное значение для
      // открытой сети, поэтому послабление действует только пока SSID тот же.
      // Указали другую сеть и оставили поле пустым -- значит, она открытая.
      String ssid = portal.getString("ssid");
      String pass = portal.getString("pass");
      bool sameNetwork = ssid == settings::getStringValue(keys::wifi::ssid);

      settings::setString(keys::wifi::ssid, ssid.c_str());
      if (pass.length() || !sameNetwork)
        settings::setString(keys::wifi::password, pass.c_str());
      settings::commit();
      // Без перезагрузки: автомат сам попробует новые креды, а если они
      // не подойдут -- вернёт точку доступа.
      corewifi::credentialsChanged();

    // Factory reset
    } else if(portal.form(form.factoryReset)){
      Serial.println("Factory reset");
      if(portal.getCheck("resetAllow"))
        factoryReset();

    // Preferences
    } else if(portal.form(form.preferences)){
      // Имя устройства задаёт топики MQTT, hostname и имя точки доступа,
      // а строятся они один раз на загрузке -- поэтому смена имени требует
      // перезагрузки. Остальные настройки на этой странице по-прежнему
      // применяются на лету.
      //
      // Прежнее имя запоминается, чтобы уже после перезагрузки снять с
      // брокера опубликованное под ним: снимать здесь бесполезно, завещание
      // вернёт часть обратно. Разбор -- в mqttClearPreviousName().
      String deviceName = portal.getString("deviceName");
      String prevName = settings::getStringValue(keys::dev::name);
      bool nameChanged = deviceName != prevName;
      if (nameChanged) settings::setString(keys::mqtt::prevName, prevName.c_str());

      settings::setString(keys::dev::name, deviceName.c_str());

      device::readSettingsForm();

      int32_t timezone = portal.getInt("timezone");
      settings::setInt(keys::dev::timezone, timezone);
      corentp::setOffsetFromSettings(tzOffsetSeconds(timezone));

      bool prevAuthEnabled = settings::getBool(keys::portal::authEnabled);
      String prevAuthUsername = settings::getStringValue(keys::portal::username);
      String prevAuthPassword = settings::getStringValue(keys::portal::password);
      bool requestedAuth = portal.getCheck("authEnabled");
      String authUsername = portal.getString("authUsername");
      String authPassword = portal.getString("authPassword");
      bool sameAuthUser = authUsername == prevAuthUsername;

      // Пустой пароль сохраняет прежний только для того же логина. При смене
      // логина пустое поле действительно очищает пароль -- иначе новый
      // пользователь незаметно получил бы секрет прежнего.
      settings::setString(keys::portal::username, authUsername.c_str());
      if (authPassword.length() || !sameAuthUser)
        settings::setString(keys::portal::password, authPassword.c_str());

      bool hasAuthPassword = authPassword.length() ||
                             (sameAuthUser && prevAuthPassword.length());
      bool authEnabled = requestedAuth && authUsername.length() && hasAuthPassword;
      settings::setBool(keys::portal::authEnabled, authEnabled);
      if (requestedAuth && !authEnabled)
        println("Portal authorization needs username and password");

      bool authChanged = authEnabled != prevAuthEnabled ||
                         authUsername != prevAuthUsername ||
                         authPassword.length();

      settings::commit();

      // GyverPortal хранит указатели на учётные данные, поэтому смена режима
      // или credentials применяется после безопасной отложенной перезагрузки.
      if (nameChanged || authChanged) restartRequest();

      //MQTT Config
    } else if(portal.form(form.mqttConfig)){
      settings::setString(keys::mqtt::host, portal.getString("mqttServerIp").c_str());
      settings::setInt(keys::mqtt::port, portal.getInt("mqttServerPort"));
      // Пустое поле -- «оставить прежний пароль», как и у WiFi выше, и по тому
      // же правилу: пароль привязан к логину. Оставили логин прежним -- пустое
      // поле ничего не меняет; сменили логин (в том числе стёрли его, переезжая
      // на брокер без авторизации) -- пустое поле означает пустой пароль.
      // Адрес брокера в это правило не входит: сменить IP того же брокера --
      // дело обычное, и терять на этом пароль было бы неприятно.
      String mqttUsername = portal.getString("mqttUsername");
      String mqttPassword = portal.getString("mqttPassword");
      bool sameUser = mqttUsername == settings::getStringValue(keys::mqtt::username);

      settings::setString(keys::mqtt::username, mqttUsername.c_str());
      if (mqttPassword.length() || !sameUser)
        settings::setString(keys::mqtt::password, mqttPassword.c_str());
      settings::setInt(keys::mqtt::availableDelay, portal.getInt("available_delay"));
      settings::setInt(keys::mqtt::statusDelay, portal.getInt("status_delay"));
      settings::setString(keys::mqtt::topicPrefix, portal.getString("topicPrefix").c_str());

      restartRequest();

    } else {
      device::handleForm();
    }
  }

  if (portal.update()){
    long rssi = WiFi.RSSI();
    int strength = map(rssi, -80, -20, 0, 100);
    String wifiStrength = String(strength)+"%";
    portal.updateString("signal", wifiStrength);

    portal.updateInt("mqttStatusLed",mqttClient.isConnected());
    String ipAdress = WiFi.localIP().toString();
    portal.updateString("ipAddress", ipAdress);

    portal.updateInt("wifiAPTimer", corewifi::retryLeftSeconds());

    String time = corentp::formattedTime();
    portal.updateString("time", time);

    device::updateUi();
    portal.updateLog(glog);
  }
}


void portalAction(){
  portalCheckForm();

  if (portal.click()){
    // Имя элемента, а не голый факт клика: щелчки реле приходили и тогда,
    // когда никто ничего не нажимал, а по одному "Portal click" источник не
    // отличить. println вместо Serial.println -- чтобы строка попадала и в
    // лог портала, то есть была видна без подключения к serial.
    println("Portal click: " + portal.clickName());

    device::handleClick();
    // Через заказ, хотя именно здесь можно было и напрямую: клику GyverPortal
    // отвечает server.send(200) ДО вызова _action() (portal.h:122), в отличие
    // от формы, чей ответ собирается после (portal.h:223). То есть кнопка
    // Reboot и не висла. Но единственная дорога к перезагрузке надёжнее двух:
    // разница между этими путями видна только в исходниках библиотеки.
    if (portal.click("rebootButton")){ restartRequest(); }
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
