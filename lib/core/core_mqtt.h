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
  // Идентификатор клиента для брокера -- не то же самое, что имя устройства.
  String clientId;
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


// Топик по произвольному имени устройства. Отдельно от topicCreate(), потому
// что снимать retained-сообщения приходится по ПРЕЖНЕМУ имени, а оно к тому
// моменту в настройках уже не хранится как текущее.
//
// Компонент HomeAssistant задаёт устройство: у реле это switch, у датчика
// sensor. Правила именования общие, поэтому топики строит ядро.
String topicFor(const String& deviceName, const char* leaf){
  char safeName[64];
  sanitizeTopicSegment(deviceName.c_str(), safeName, sizeof(safeName));

  return mqttData.connection.topicPrefix + "/" + device::haComponent() + "/" +
         safeName + "/" + leaf;
}


void topicCreate(){
  char safeName[64];
  sanitizeTopicSegment(mqttData.connection.clientName.c_str(), safeName, sizeof(safeName));
  mqttData.topicName = safeName;

  const String& deviceName = mqttData.connection.clientName;

  mqttData.topic.discovery = topicFor(deviceName, "config");
  mqttData.topic.avaible = topicFor(deviceName, "avaible");
  mqttData.topic.state = topicFor(deviceName, "state");
  mqttData.topic.command = topicFor(deviceName, "set");

  #ifdef DEBUG_MQTT
    println("MQTT discovery topic: "+ mqttData.topic.discovery );
    println("MQTT avaible topic: "+ mqttData.topic.avaible );
    println("MQTT state topic: "+ mqttData.topic.state );
    println("MQTT command topic: "+ mqttData.topic.command );
  #endif  

}


void clientIdCreate(){
  // Идентификатор клиента для брокера, а не отображаемое имя. Прежде сюда
  // уходило имя устройства как есть, а по умолчанию оно одинаково у всех
  // железок одной модели ("ESP Relay"). Брокер обязан разорвать прежнюю
  // сессию, когда подключается второй клиент с тем же идентификатором, --
  // две одинаково названные железки ушли бы в вечную карусель
  // переподключений, и диагностируется это отвратительно.
  //
  // Спецификация 3.1.1 обязывает брокер принять лишь 23 символа. Почти все
  // принимают длиннее, но закладываться на это незачем: chip ID занимает
  // шесть символов, подчёркивание одно, имени остаётся шестнадцать. Различает
  // устройства всё равно chip ID, а имя здесь только для читаемости в логе
  // брокера.
  mqttData.clientId = mqttData.topicName.substring(0, 16) + "_" +
                      String(ESP.getChipId(), HEX);

  // В лог, а не под DEBUG_MQTT: именно по этой строке в брокере опознаётся
  // устройство, и она же единственное доказательство, что идентификаторы
  // двух железок разошлись.
  println("MQTT client id: " + mqttData.clientId);
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
  clientIdCreate();

  mqttClient.setMqttServer( mqttData.connection.serverIp.c_str(),
                            mqttData.connection.username.c_str(),
                            mqttData.connection.password.c_str(),
                            mqttData.connection.serverPort
                           );
  // Указатель сохраняется как есть, поэтому строка обязана пережить клиента --
  // та же причина, что и у завещания ниже.
  mqttClient.setMqttClientName(mqttData.clientId.c_str());
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


// Пустая полезная нагрузка с retain -- принятый способ снять сохранённое
// сообщение: брокер выбрасывает его из хранилища, а HomeAssistant на пустой
// config убирает автообнаруженную сущность.
//
// Без этого смена имени устройства оставляла в HomeAssistant вечный призрак:
// топики строятся из имени, и после переименования старый config продолжал
// висеть у брокера -- сущность, в которую больше никто никогда не опубликует.
void mqttClearRetainedFor(const String& deviceName){
  println("MQTT clearing retained topics for " + deviceName);
  mqttClient.publish(topicFor(deviceName, "config"), "", true);
  mqttClient.publish(topicFor(deviceName, "state"), "", true);
  mqttClient.publish(topicFor(deviceName, "avaible"), "", true);
  mqttClient.loop();
}


void mqttClearRetained(){
  if (!mqttClient.isConnected()){
    println("MQTT is offline, retained topics left as is");
    return;
  }
  mqttClearRetainedFor(mqttData.connection.clientName);
}


// Уборка за переименованием -- уже после перезагрузки, на свежем подключении.
//
// Сделать это в момент смены имени не выходит: перезагрузка рвёт соединение,
// и брокер тут же публикует завещание в старый avaible, возвращая только что
// убранное. Корректно попрощаться с брокером тоже нельзя -- EspMQTTClient
// держит disconnect() при себе. Поэтому прежнее имя переживает перезагрузку
// в настройках, а снимаем мы его топики здесь, когда завещание давно
// отработало.
//
// Заодно это чинит случай, когда в момент переименования MQTT был недоступен:
// уборка просто произойдёт при следующем подключении.
void mqttClearPreviousName(){
  String prev = settings::getStringValue(keys::mqtt::prevName);
  if (!prev.length()) return;

  // Санация имён могла схлопнуть разные имена в один топик -- тогда снимать
  // нечего, мы бы стёрли собственные свежие сообщения.
  if (topicFor(prev, "config") != getDiscoveryTopic())
    mqttClearRetainedFor(prev);

  settings::setString(keys::mqtt::prevName, "");
  settings::commit();
}


void onConnectionEstablished() {
  println("MQTT server is connected");
  SendDiscoveryMessage();
  // Состояние сразу за автообнаружением, не дожидаясь таймера: иначе HA
  // получает сущность и держит её unknown до ближайшего периодического
  // сообщения -- десять секунд карточки, которая не знает, включено ли реле.
  publishState();
  SendAvailableMessage("online");

  // Уборка за переименованием откладывается, а не делается здесь. Брокер
  // объявляет прежнюю сессию мёртвой не сразу, а по истечении keep-alive с
  // половиной сверху (15 с у PubSubClient, то есть около 22 с), и только
  // тогда публикует завещание в старый avaible. Уберись мы прямо сейчас --
  // завещание пришло бы следом и вернуло мусор. Проверено на железе: ровно
  // так и происходило.
  if (settings::getStringValue(keys::mqtt::prevName).length()){
    CleanupTimer.setTimerMode();
    CleanupTimer.setTime(30000);
    CleanupTimer.start();
  }

  mqttClient.subscribe(getCommandTopic(), [] (const String &payload)  {
    println("MQTT received command topic");
    device::onCommand(payload);
  });
}


void publishState() {
  if (!mqttClient.isConnected()){
    return;
  };
  #ifdef DEBUG_MQTT
    println("MQTT publish status");
  #endif
  JsonDocument doc;
  doc["WiFiRSSI"] = WiFi.RSSI();
  doc["IPAddress"] = WiFi.localIP().toString();
  device::fillState(doc);

  String payload;
  serializeJson(doc, payload);

  // retain=true по той же причине, что и у avaible: без него перезапущенный
  // HomeAssistant подписывается на пустой топик и держит сущность unknown,
  // пока не придёт очередное периодическое сообщение.
  if (!mqttClient.publish(getStateTopic(), payload, true))
    println("MQTT state publish failed, size "+String(payload.length()));
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

  // Всё, что зависит от вида устройства -- команды, шаблон значения, класс,
  // единицы измерения -- добавляет само устройство.
  device::fillDiscovery(doc);

  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = device_name;
  device["model"] = String("ESP_") + ::device::model() + "_hw1.0";
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
    publishState();
  }

  if (mqttClient.isConnected() && ServiceMessageTimer.tick()) {
    SendAvailableMessage();
  }

  // Одноразовый: запускается только после переименования, снимает топики
  // прежнего имени и больше не взводится.
  if (mqttClient.isConnected() && CleanupTimer.tick()) {
    mqttClearPreviousName();
  }
}
