struct MQTTConnection {
  String serverIp;
  uint16_t serverPort;
  String username;
  String password;
  String clientName;
  String topicPrefix;
  uint32_t status_delay;
  uint32_t avaible_delay;
};

struct MQTTTopic{
  String discovery;
  String command;
  String avaible;
  String state;
};

struct MQTTData{
  MQTTConnection connection;
  MQTTTopic topic;
  // Имя устройства, приведённое к допустимым в топике символам.
  // Отображаемое имя остаётся в connection.clientName как есть.
  String topicName;
};


MQTTData mqttData;


void mqttReadConfig() {
  // Настройки читаются один раз при старте и оседают в mqttData: строки
  // подключения нужны клиенту на всё время работы, а буфер топика ещё и
  // должен пережить сам клиент -- завещание хранит на него сырой указатель.
  mqttData.connection.serverIp = settings::getStringValue(keys::mqtt::host);
  mqttData.connection.serverPort = settings::getInt(keys::mqtt::port);
  mqttData.connection.username = settings::getStringValue(keys::mqtt::username);
  mqttData.connection.password = settings::getStringValue(keys::mqtt::password);
  mqttData.connection.clientName = settings::getStringValue(keys::dev::name);
  mqttData.connection.topicPrefix = settings::getStringValue(keys::mqtt::topicPrefix);
  mqttData.connection.status_delay = settings::getInt(keys::mqtt::statusDelay);
  mqttData.connection.avaible_delay = settings::getInt(keys::mqtt::availableDelay);
}


void topicCreate(){
  String topicPrefix = mqttData.connection.topicPrefix;

  char safeName[64];
  sanitizeTopicSegment(mqttData.connection.clientName.c_str(), safeName, sizeof(safeName));
  mqttData.topicName = safeName;
  const String& deviceName = mqttData.topicName;

  mqttData.topic.discovery = topicPrefix + "/switch/" + deviceName + "/config";
  mqttData.topic.avaible = topicPrefix + "/switch/" + deviceName + "/avaible";
  mqttData.topic.state = topicPrefix + "/switch/" + deviceName + "/state";
  mqttData.topic.command = topicPrefix + "/switch/" + deviceName + "/set";

  #ifdef DEBUG_MQTT
    println("MQTT discovery topic: "+ mqttData.topic.discovery );
    println("MQTT avaible topic: "+ mqttData.topic.avaible );
    println("MQTT state topic: "+ mqttData.topic.state );
    println("MQTT command topic: "+ mqttData.topic.command );
  #endif  

}


// Возврат по ссылке, а не по значению: топики публикуются каждые 10 секунд,
// и копия String на каждый вызов это лишняя аллокация в куче. Кроме того,
// enableLastWillMessage() запоминает сырой указатель на буфер строки.
const String& getDiscoveryTopic(){
  return mqttData.topic.discovery;
}


const String& getCommandTopic(){
  return mqttData.topic.command;
}


const String& getAvaibleTopic(){
  return mqttData.topic.avaible;
}


const String& getStateTopic(){
  return mqttData.topic.state;
}


void mqttStart(){
  println("Starting MQTT"); 

  mqttReadConfig();

  #ifdef DEBUG_MQTT
    mqttClient.enableDebuggingMessages();
  #endif  

  //Create topics
  topicCreate();

  mqttClient.setMqttServer( mqttData.connection.serverIp.c_str(), 
                            mqttData.connection.username.c_str(), 
                            mqttData.connection.password.c_str(),
                            mqttData.connection.serverPort 
                           );
  mqttClient.setMqttClientName(mqttData.connection.clientName.c_str());
  //Setup max lingth of message MQTT
  mqttClient.setMaxPacketSize(2048);

  // Завещание брокеру. Без него пропавшее устройство навсегда остаётся
  // online в HomeAssistant: периодический avaible просто перестаёт приходить,
  // а сказать об этом некому.
  // Указатель сохраняется как есть, поэтому строка обязана пережить клиента:
  // берём буфер глобального mqttData, заполненный в topicCreate() выше.
  mqttClient.enableLastWillMessage(mqttData.topic.avaible.c_str(), "offline", true);

  // MQTT timers
  println("Starting MQTT timers");
  MessageTimer.setTime(mqttData.connection.status_delay * 1000);
  MessageTimer.start();
  ServiceMessageTimer.setTime(mqttData.connection.avaible_delay * 1000);
  ServiceMessageTimer.start();
}


void onConnectionEstablished() {
  println("MQTT server is connected");
  SendDiscoveryMessage();
  SendAvailableMessage("online");

  mqttClient.subscribe(getCommandTopic(), [] (const String &payload)  {
    println("MQTT received command topic");
    Relay1.SetState( parseSwitchPayload(payload.c_str(), Relay1.GetState()) );
  });
}


void publishRelay() {
  if (!mqttClient.isConnected()){
    return;
  };
  #ifdef DEBUG_MQTT
    println("MQTT publish status");
  #endif
  JsonDocument doc;
  doc["switch"] = Relay1.GetState();
  doc["WiFiRSSI"] = WiFi.RSSI();
  doc["IPAddress"] = WiFi.localIP().toString();

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(getStateTopic(), payload, false);
}


void SendDiscoveryMessage( ){
  #ifdef DEBUG_MQTT
    println("MQTT publish discovery message");
  #endif
  // Раньше документ сериализовался в char[1024]. Полезная нагрузка с длинным
  // именем устройства подбиралась к этому пределу вплотную, а serializeJson
  // при нехватке места молча обрезает вывод -- в брокер уходил битый JSON.
  // JsonDocument растёт по месту, payload тоже, обрезать нечему.
  JsonDocument doc;

  String device_name = mqttData.connection.clientName;
  uint32_t chipId = ESP.getChipId();

  doc["name"]         = device_name;
  doc["uniq_id"]      = chipId;
  doc["object_id"]    = "ESP_"+mqttData.topicName+"_"+WiFi.macAddress();
  doc["ip"]           = WiFi.localIP().toString();
  doc["mac"]          = WiFi.macAddress();
  doc["avty_t"]       = getAvaibleTopic();
  doc["pl_avail"]     = "online";
  doc["pl_not_avail"] = "offline";
  // Явные строки вместо булевых значений. Раньше здесь лежали JSON true/false,
  // и работало это лишь по совпадению: HomeAssistant приводит их к строкам
  // "True"/"False", ровно так же, как Jinja рендерит булево значение в шаблоне.
  doc["stat_t"]       = getStateTopic();
  doc["stat_on"]      = "ON";
  doc["stat_off"]     = "OFF";
  doc["cmd_t"]        = getCommandTopic();
  doc["pl_on"]        = "ON";
  doc["pl_off"]       = "OFF";
  doc["dev_cla"]      = "switch";
  doc["val_tpl"]      = "{{ 'ON' if value_json.switch else 'OFF' }}";

  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = device_name;
  device["model"] = "ESP_" + device_name + "_hw1.0";
  device["configuration_url"] = "http://"+WiFi.localIP().toString();
  device["manufacturer"] = "WhiteFoot company";
  device["sw_version"]   = sw_version;

  //JsonArray connections = device.createNestedArray("connections");
  //connections.add("ip,"+ WiFi.localIP().toString());
  //connections.add("mac,"+ WiFi.macAddress());

  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(WiFi.macAddress());

  String payload;
  serializeJson(doc, payload);

  // Сообщение крупное, и при превышении буфера PubSubClient молча его не
  // отправит: без явной проверки автообнаружение отваливается беззвучно.
  if (!mqttClient.publish(getDiscoveryTopic(), payload, true))
    println("MQTT discovery publish failed, size "+String(payload.length()));
}


void SendAvailableMessage(const String &mode = "online"){
  #ifdef DEBUG_MQTT
    println("MQTT publish available message");
  #endif
  // retain=true, иначе после перезапуска HomeAssistant сущность висит
  // unavailable до следующего периодического сообщения.
  mqttClient.publish(getAvaibleTopic(), mode, true);
}


void mqttPublish() {
  if (mqttClient.isConnected() && MessageTimer.tick()) {
    publishRelay();
  }

  if (mqttClient.isConnected() && ServiceMessageTimer.tick()) {
    SendAvailableMessage();
  }
}
