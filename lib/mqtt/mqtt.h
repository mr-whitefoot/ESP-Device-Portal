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
};


MQTTData mqttData;


void mqttReadConfig() {
  mqttData.connection.serverIp = data.mqttServerIp;
  mqttData.connection.serverPort = data.mqttServerPort;
  mqttData.connection.username = data.mqttUsername;
  mqttData.connection.password = data.mqttPassword;
  mqttData.connection.clientName = data.deviceName;
  mqttData.connection.topicPrefix = data.mqttTopicPrefix;
  mqttData.connection.status_delay = data.mqttStatusDelay;
  mqttData.connection.avaible_delay = data.mqttAvaibleDelay;
}


void topicCreate(){
  String topicPrefix = mqttData.connection.topicPrefix;
  String deviceName = mqttData.connection.clientName;

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


const String getDiscoveryTopic(){
  return mqttData.topic.discovery;
}


const String getCommandTopic(){
  return mqttData.topic.command;
}


const String getAvaibleTopic(){
  return mqttData.topic.avaible;
}


const String getStateTopic(){
  return mqttData.topic.state;
}


bool ToBool( String value){
  if ( (value == "true" )||
       (value == "True" )||
       (value == "TRUE" )){
        return true;
       };
  
  if ( value == "false" ||
       value == "False" ||
       value == "FALSE" ){
        return false;
       };
  
  return false;
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
    Relay1.SetState( ToBool(payload));
  });
}


void publishRelay() {
  if (!mqttClient.isConnected()){
    return;
  };
  #ifdef DEBUG_MQTT
    println("MQTT publish status");
  #endif
  DynamicJsonDocument doc(256);
  char buffer[256];
  doc["switch"] = Relay1.GetState();
  doc["WiFiRSSI"] = WiFi.RSSI(); 
  doc["IPAddress"] = WiFi.localIP().toString();

  serializeJson(doc, buffer);
  mqttClient.publish(getStateTopic(), buffer, false);
}


void SendDiscoveryMessage( ){
  #ifdef DEBUG_MQTT
    println("MQTT publish discovery message");
  #endif
  DynamicJsonDocument doc(1024);
  char buffer[1024];

  String device_name = mqttData.connection.clientName;
  uint32_t chipId = ESP.getChipId();

  doc["name"]         = device_name;
  doc["uniq_id"]      = chipId;
  doc["object_id"]    = "ESP_"+device_name+"_"+WiFi.macAddress();
  doc["ip"]           = WiFi.localIP().toString();
  doc["mac"]          = WiFi.macAddress();
  doc["avty_t"]       = getAvaibleTopic();
  doc["pl_avail"]     = "online";
  doc["pl_not_avail"] = "offline";
  doc["stat_t"]       = getStateTopic();
  doc["stat_on"]      = true;
  doc["stat_off"]     = false;
  doc["cmd_t"]        = getCommandTopic();
  doc["pl_on"]        = true;
  doc["pl_off"]       = false;
  doc["dev_cla"]      = "switch";
  doc["val_tpl"]      = "{{ value_json.switch|default(false) }}";

  JsonObject device = doc.createNestedObject("device");
  device["name"] = device_name;
  device["model"] = "ESP_" + device_name + "_hw1.0";
  device["configuration_url"] = "http://"+WiFi.localIP().toString();
  device["manufacturer"] = "WhiteFoot company";
  device["sw_version"]   = sw_version;

  //JsonArray connections = device.createNestedArray("connections");
  //connections.add("ip,"+ WiFi.localIP().toString());
  //connections.add("mac,"+ WiFi.macAddress());

  JsonArray identifiers = device.createNestedArray("identifiers");
  identifiers.add(WiFi.macAddress());

  serializeJson(doc, buffer);
  mqttClient.publish(getDiscoveryTopic(), buffer, true);
}


void SendAvailableMessage(const String &mode = "online"){
  #ifdef DEBUG_MQTT
    println("MQTT publish available message");
  #endif
  mqttClient.publish(getAvaibleTopic(), mode, false);
}


void mqttPublish() {
  if (mqttClient.isConnected() && MessageTimer.tick()) {
    publishRelay();
  }

  if (mqttClient.isConnected() && ServiceMessageTimer.tick()) {
    SendAvailableMessage();
  }
}
