struct MQTTConnection {
  String serverIp;
  uint16_t serverPort;
  String username;
  String password;
  String clientName;
  String topicPrefix;
  uint32_t status_delay;
  uint32_t available_delay;
};

struct MQTTTopic{
  String available;
  String state;
};

struct MQTTEntityTopic{
  String discovery;
  String command;
};

// Лист топика доступности до 3.6.0. Опечатка жила в именах топиков с самого
// начала, и исправление ломает совместимость -- но ломает мягко: HA берёт
// адрес из avty_t в discovery, то есть переезжает сам. У брокера остаётся
// только сохранённое сообщение по старому адресу, и его надо снять.
static const char* LEGACY_AVAILABLE_LEAF = "avaible";

struct MQTTData{
  MQTTConnection connection;
  MQTTTopic topic;
  MQTTEntityTopic entity[device::MAX_ENTITIES];
  uint8_t entityCount;
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
  mqttData.connection.available_delay = settings::getInt(keys::mqtt::availableDelay);
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


// Discovery и команды нескольких сущностей получают дополнительный стабильный
// сегмент. Для единственной сущности ветка намеренно возвращает прежний адрес,
// чтобы обновление существующих устройств не создало дубликаты в HA.
String topicForEntity(const String& deviceName, uint8_t entity, const char* leaf){
  if (mqttData.entityCount <= 1) return topicFor(deviceName, leaf);

  char safeName[64];
  char safeEntity[32];
  sanitizeTopicSegment(deviceName.c_str(), safeName, sizeof(safeName));
  sanitizeTopicSegment(device::entityId(entity), safeEntity, sizeof(safeEntity));

  return mqttData.connection.topicPrefix + "/" + device::haComponent() + "/" +
         safeName + "/" + safeEntity + "/" + leaf;
}


void topicCreate(){
  char safeName[64];
  sanitizeTopicSegment(mqttData.connection.clientName.c_str(), safeName, sizeof(safeName));
  mqttData.topicName = safeName;

  mqttData.entityCount = device::entityCount();
  if (mqttData.entityCount == 0 || mqttData.entityCount > device::MAX_ENTITIES) {
    LOG_E(mqtt, String(F("invalid entity count=")) + mqttData.entityCount);
    mqttData.entityCount = mqttData.entityCount == 0 ? 1 : device::MAX_ENTITIES;
  }

  const String& deviceName = mqttData.connection.clientName;
  mqttData.topic.available = topicFor(deviceName, "available");
  mqttData.topic.state = topicFor(deviceName, "state");
  for (uint8_t i = 0; i < mqttData.entityCount; i++) {
    mqttData.entity[i].discovery = topicForEntity(deviceName, i, "config");
    mqttData.entity[i].command = topicForEntity(deviceName, i, "set");
  }

  // Уровень D: топики строятся по одному правилу из имени устройства, и в
  // норме достаточно знать имя. Нужны они, когда HomeAssistant не видит
  // сущность, -- тогда сборка с -D LOG_LEVEL=4 показывает все четыре адреса.
  LOG_D(mqtt, String(F("topic available=")) + mqttData.topic.available);
  LOG_D(mqtt, String(F("topic state=")) + mqttData.topic.state);
  for (uint8_t i = 0; i < mqttData.entityCount; i++) {
    LOG_D(mqtt, String(F("topic discovery[")) + i + "]=" +
                mqttData.entity[i].discovery);
    LOG_D(mqtt, String(F("topic command[")) + i + "]=" +
                mqttData.entity[i].command);
  }
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

  // Уровень I, а не D: именно по этой строке в брокере опознаётся устройство,
  // и она же единственное доказательство, что идентификаторы двух железок
  // разошлись.
  LOG_I(mqtt, String(F("client id=")) + mqttData.clientId);
}


// Возврат по ссылке, а не по значению: топики публикуются каждые 10 секунд,
// и копия String на каждый вызов это лишняя аллокация в куче. Кроме того,
// enableLastWillMessage() запоминает сырой указатель на буфер строки.
const String& getDiscoveryTopic(uint8_t entity = 0){
  if (entity >= mqttData.entityCount) entity = 0;
  return mqttData.entity[entity].discovery;
}


const String& getCommandTopic(uint8_t entity = 0){
  if (entity >= mqttData.entityCount) entity = 0;
  return mqttData.entity[entity].command;
}


const String& getAvailableTopic(){
  return mqttData.topic.available;
}


const String& getStateTopic(){
  return mqttData.topic.state;
}


void mqttStart(){
  mqttReadConfig();

  // Адрес брокера в логе -- первое, что спрашивают у неподключающегося
  // устройства, и первое, что при этом оказывается не тем, чем считалось.
  LOG_I(mqtt, String(F("init broker=")) + mqttData.connection.serverIp + ":" +
              mqttData.connection.serverPort +
              F(" prefix=") + mqttData.connection.topicPrefix);
  LOG_D(mqtt, String(F("periods state=")) + mqttData.connection.status_delay +
              F("s available=") + mqttData.connection.available_delay + "s");

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
  // Тег ставит ядро, а не транспорт: сам транспорт не знает, под каким именем
  // его подсистема называется в логе.
  mqttClient.setLogCallback([](char level, const String& text){
    corelog::writeChar(level, corelog::tag::mqtt, text);
  });
  mqttClient.setOnConnectionEstablishedCallback(onConnectionEstablished);

  // Завещание брокеру. Без него пропавшее устройство навсегда остаётся
  // online в HomeAssistant: периодическое available просто перестаёт
  // приходить, а сказать об этом некому.
  // Указатель сохраняется как есть, поэтому строка обязана пережить клиента:
  // берём буфер глобального mqttData, заполненный в topicCreate() выше.
  mqttClient.enableLastWillMessage(mqttData.topic.available.c_str(), "offline", true);

  // MQTT timers
  MessageTimer.setTime(mqttData.connection.status_delay * 1000);
  MessageTimer.start();
  ServiceMessageTimer.setTime(mqttData.connection.available_delay * 1000);
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
  LOG_I(mqtt, String(F("clearing retained name=")) + deviceName);
  for (uint8_t i = 0; i < mqttData.entityCount; i++)
    mqttClient.publish(topicForEntity(deviceName, i, "config"), "", true);
  mqttClient.publish(topicFor(deviceName, "state"), "", true);
  mqttClient.publish(topicFor(deviceName, "available"), "", true);
  // Топик с прежней опечаткой снимается и здесь. Устройство, которое
  // переименовали ДО обновления, хранит старое имя в prevName, и уборка за
  // ним доедет уже с новыми листьями -- сообщение по адресу с опечаткой
  // осталось бы у брокера навсегда, снять его вручную можно только через
  // сам брокер.
  mqttClient.publish(topicFor(deviceName, LEGACY_AVAILABLE_LEAF), "", true);
  mqttClient.loop();
}


void mqttClearRetained(){
  if (!mqttClient.isConnected()){
    LOG_W(mqtt, F("offline, retained topics left as is"));
    return;
  }
  mqttClearRetainedFor(mqttData.connection.clientName);
}


// Уборка за переименованием -- уже после перезагрузки, на свежем подключении.
//
// Сделать это в момент смены имени не выходит: перезагрузка рвёт соединение,
// и брокер тут же публикует завещание в старый avaible, возвращая только что
// убранное. Прежнее имя переживает обычную перезагрузку в настройках, а его
// топики снимаются здесь, когда завещание уже отработало. Поведение factory
// reset при этом намеренно не меняется.
//
// Заодно это чинит случай, когда в момент переименования MQTT был недоступен:
// уборка просто произойдёт при следующем подключении.
void mqttClearPreviousName(){
  String prev = settings::getStringValue(keys::mqtt::prevName);
  if (!prev.length()) return;

  // Санация имён могла схлопнуть разные имена в один топик -- тогда снимать
  // нечего, мы бы стёрли собственные свежие сообщения.
  if (topicFor(prev, "state") != getStateTopic())
    mqttClearRetainedFor(prev);

  settings::setString(keys::mqtt::prevName, "");
  settings::commit();
}


// Полное объявление себя брокеру: сущность, состояние, доступность.
//
// Состояние идёт сразу за автообнаружением, не дожидаясь таймера: иначе HA
// получает сущность и держит её unknown до ближайшего периодического
// сообщения -- десять секунд карточки, которая не знает, включено ли реле.
void mqttAnnounce(){
  SendDiscoveryMessage();
  publishState();
  SendAvailableMessage("online");
}


// Снять прежнюю сущность перед тем, как объявить новую. Одноразовое действие
// при обновлении со схемы 1.
//
// В 3.6.0 сменились uniq_id и object_id, а HomeAssistant не переносит их на
// уже заведённую сущность: прежняя осталась бы висеть недоступной, а новая
// получила бы entity_id с суффиксом _2. Пустой config убирает сущность из
// реестра, и место освобождается.
//
// Топик доступности с опечаткой снимается НЕ здесь, а в отложенной уборке:
// перезагрузка на обновлении рвёт прежнюю сессию, и брокер кладёт по этому
// адресу завещание -- но не сразу, а через keep-alive с половиной сверху.
// Уберись мы сейчас, завещание вернуло бы мусор следом.
void mqttRetirePreviousEntity(){
  LOG_I(mqtt, F("retiring previous entity after schema upgrade"));
  for (uint8_t i = 0; i < mqttData.entityCount; i++)
    mqttClient.publish(getDiscoveryTopic(i), "", true);
  mqttClient.loop();
}


// Снять сообщение по топику доступности с прежней опечаткой. Зовётся из
// отложенной уборки, когда завещание прежней сессии давно отработало.
void mqttClearLegacyAvailable(){
  if (!settings::getBool(keys::mqtt::rediscover)) return;

  LOG_I(mqtt, F("clearing legacy availability topic"));
  mqttClient.publish(topicFor(mqttData.connection.clientName, LEGACY_AVAILABLE_LEAF),
                     "", true);
  mqttClient.loop();

  settings::setBool(keys::mqtt::rediscover, false);
  settings::commit();
}


void onConnectionEstablished() {
  LOG_I(mqtt, F("connected"));

  // Объявиться заново можно только после того, как HA переварит удаление,
  // поэтому на обновлении discovery откладывается. Дальше объявление идёт
  // как обычно -- сразу.
  if (settings::getBool(keys::mqtt::rediscover)){
    mqttRetirePreviousEntity();
    RediscoverTimer.setTimerMode();
    RediscoverTimer.setTime(3000);
    RediscoverTimer.start();
  } else {
    mqttAnnounce();
  }

  // Уборка откладывается, а не делается здесь. Брокер объявляет прежнюю
  // сессию мёртвой не сразу, а по истечении keep-alive с половиной сверху
  // (у AsyncMqttClient по умолчанию 15 с), и только тогда публикует
  // завещание в топик доступности прежней прошивки. Уберись мы прямо
  // сейчас -- завещание пришло бы следом и вернуло мусор. Проверено на
  // железе: ровно так и происходило.
  //
  // Поводов два, и оба ждут одного и того же: топики прежнего имени после
  // переименования и топик доступности с прежней опечаткой после обновления.
  if (settings::getStringValue(keys::mqtt::prevName).length() ||
      settings::getBool(keys::mqtt::rediscover)){
    CleanupTimer.setTimerMode();
    CleanupTimer.setTime(30000);
    CleanupTimer.start();
  }

  // Полезная нагрузка в строке, а не просто факт прихода команды: раньше в
  // логе стояло "received command topic", и по нему нельзя было отличить
  // команду, которую устройство не поняло, от команды, которую оно поняло и
  // выполнило не так, как ждал отправитель.
  for (uint8_t i = 0; i < mqttData.entityCount; i++) {
    bool subscribed = mqttClient.subscribe(
        getCommandTopic(i), [i] (const String &payload) {
          LOG_I(mqtt, String(F("command entity=")) + i + " " + payload);
          device::onCommand(i, payload);
        });
    // Молча не подписавшееся устройство выглядит как исправное: состояние оно
    // публикует, доступность тоже, и только команды до него не доходят.
    if (!subscribed)
      LOG_W(mqtt, String(F("command subscribe failed entity=")) + i);
  }
}


void publishState() {
  if (!mqttClient.isConnected()){
    return;
  };
  LOG_D(mqtt, F("publish state"));
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
    LOG_W(mqtt, String(F("state publish failed size=")) + payload.length());
}


void SendDiscoveryMessage(uint8_t entity){
  LOG_D(mqtt, String(F("publish discovery entity=")) + entity);
  // Раньше документ сериализовался в char[1024]. Полезная нагрузка с длинным
  // именем устройства подбиралась к этому пределу вплотную, а serializeJson
  // при нехватке места молча обрезает вывод -- в брокер уходил битый JSON.
  // JsonDocument растёт по месту, payload тоже, обрезать нечему.
  JsonDocument doc;

  String device_name = mqttData.connection.clientName;
  char displayName[64];
  device::entityName(entity, displayName, sizeof(displayName));

  // uniq_id -- на сущность, а не на устройство: раньше здесь было голое
  // число chipId, общее для всего, что живёт на этом чипе. Разбор в
  // buildUniqueId().
  char uniqueId[64];
  if (mqttData.entityCount == 1)
    buildUniqueId(ESP.getChipId(), device::haComponent(), uniqueId, sizeof(uniqueId));
  else
    buildEntityUniqueId(ESP.getChipId(), device::haComponent(),
                        device::entityId(entity), uniqueId, sizeof(uniqueId));

  doc["name"]         = displayName;
  doc["uniq_id"]      = uniqueId;
  // object_id -- это подсказка HomeAssistant, каким сделать entity_id.
  // Раньше сюда уходило "ESP_" плюс имя плюс MAC с двоеточиями, и получалось
  // switch.esp_esp_relay_dc_4f_22_4d_21_97: нечитаемо, с удвоенным esp_esp
  // и MAC внутри -- ровно обратное тому, зачем object_id существует.
  // Санированное имя устройства даёт switch.esp_relay.
  if (mqttData.entityCount == 1) {
    doc["object_id"] = mqttData.topicName;
  } else {
    char safeEntity[32];
    sanitizeTopicSegment(device::entityId(entity), safeEntity, sizeof(safeEntity));
    doc["object_id"] = mqttData.topicName + "_" + safeEntity;
  }
  // ip и mac на верхнем уровне убраны: в схеме HomeAssistant таких ключей
  // нет, discovery-схемы объявлены с extra=vol.REMOVE_EXTRA и молча их
  // выбрасывали. Адрес и так уходит в device.configuration_url, MAC --
  // в device.identifiers.
  doc["avty_t"]       = getAvailableTopic();
  doc["pl_avail"]     = "online";
  doc["pl_not_avail"] = "offline";
  // Явные строки вместо булевых значений. Раньше здесь лежали JSON true/false,
  // и работало это лишь по совпадению: HomeAssistant приводит их к строкам
  // "True"/"False", ровно так же, как Jinja рендерит булево значение в шаблоне.
  doc["stat_t"]       = getStateTopic();

  // Всё, что зависит от вида устройства -- команды, шаблон значения, класс,
  // единицы измерения -- добавляет само устройство.
  device::fillDiscovery(entity, doc);

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

  // Сообщение крупное, и транспорт может не принять его, например при
  // нехватке памяти: без явной проверки автообнаружение отвалится беззвучно.
  if (!mqttClient.publish(getDiscoveryTopic(entity), payload, true))
    LOG_W(mqtt, String(F("discovery publish failed entity=")) + entity +
                F(" size=") + payload.length());
}


void SendDiscoveryMessage(){
  for (uint8_t i = 0; i < mqttData.entityCount; i++)
    SendDiscoveryMessage(i);
}


void SendAvailableMessage(const String &mode = "online"){
  LOG_D(mqtt, String(F("publish available ")) + mode);
  // retain=true, иначе после перезапуска HomeAssistant сущность висит
  // unavailable до следующего периодического сообщения.
  mqttClient.publish(getAvailableTopic(), mode, true);
}




void mqttPublish() {
  // Перезагрузка заказана и вот-вот случится -- публиковать нечего и незачем.
  // Это не оптимизация: factory reset снимает retained-топики и только потом
  // отдаёт ответ на форму, а очередное периодическое сообщение, попавшее в
  // это окно, вернуло бы брокеру снятое.
  if (restartPending.pending()) return;

  if (mqttClient.isConnected() && MessageTimer.tick()) {
    publishState();
  }

  if (mqttClient.isConnected() && ServiceMessageTimer.tick()) {
    SendAvailableMessage();
  }

  // Одноразовый: отложенная уборка за переименованием и за обновлением схемы.
  // Оба флага гасятся только здесь, поэтому обновление, оборвавшееся на
  // середине, доделается при следующем подключении.
  if (mqttClient.isConnected() && CleanupTimer.tick()) {
    mqttClearPreviousName();
    mqttClearLegacyAvailable();
  }

  // Тоже одноразовый: объявление новой сущности после снятия прежней.
  if (mqttClient.isConnected() && RediscoverTimer.tick()) {
    mqttAnnounce();
  }
}
