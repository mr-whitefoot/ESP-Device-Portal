#include <EspMQTTClient.h>
#include <ArduinoJson.h>
#include <TimerMs.h>
#include <GyverPortal.h>
#include <ESPRelay.h>
#include <Timezone.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <GyverDBFile.h>
#include <LittleFS.h>
// Слой настроек пока только подключён, чтобы сборка прошивки проверяла
// GyverDB-бэкенд: тестами он не покрывается, так как требует LittleFS.
// Переезд кода на него -- следующим шагом.
#include <settings.h>
#include <timezone_table.h>
#include <mqtt_topics.h>
#include <mqtt_payload.h>
#include <timer_schedule.h>


//#define DEBUG_MQTT
//#define DEBUG_DB


// Версия приходит из platformio.ini через -DVERSION, дата сборки -- из
// extra_script.py через -DRELEASE_DATE. Так они не разъезжаются ни с
// release_version, ни с именем bin-файла, ни с реальностью.
// Макросы подставляются без кавычек, поэтому их нужно превратить в строку.
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

String sw_version = STRINGIFY(VERSION);
String release_date = STRINGIFY(RELEASE_DATE);

GyverDBFile db(&LittleFS, "/data.db");


#include <wifi_func.h>


#define RELAY_PIN 0


enum keys : size_t {
  deviceName = SH("deviceName"),
  
  relayInvertMode = SH("relayInvertMode"),
  saveRelayStatus = SH("saveRelayStatus"),
  relayState      = SH("relayState"),
  timezone        = SH("timezone"),

  timer = SH("timer"),
};

enum mqtt : size_t {
  serverIp = SH("mqttServerIp"),
  serverPort = SH("mqttServerPort"),
  
  username = SH("mqttUsername"),
  password1 = SH("mqttPassword"),

  status_delay = SH("status_delay"),
  avaible_delay = SH("avaible_delay"),

  topicPrefix = SH("topicPrefix"),
};

struct Data {
  // Device settings
  String deviceName;
  bool relayInvertMode;
  bool saveRelayStatus;
  bool relayState;
  byte timezone;

  // WiFi settings
  // Флага forceAP здесь намеренно нет: им владеет wifi-библиотека, которая
  // пишет его в базу напрямую. Копия в Data успевала устареть, и обратная
  // запись из updateConfig() затирала решение библиотеки.
  String wifiSsid;
  String wifiPass;

  // MQTT settings
  String mqttServerIp;
  uint16_t mqttServerPort;
  String mqttUsername;
  String mqttPassword;
  uint32_t mqttStatusDelay;
  uint32_t mqttAvaibleDelay;
  String mqttTopicPrefix;

  // Timers
  Timers timers; 
};


Data data;
GyverPortal portal;
GPlog glog("log");


struct Form{
  const char* root = "/";
  const char* log = "/log";
  const char* timers = "/timers";
  const char* config = "/config";
  const char* preferences = "/config/preferences";
  const char* WiFiConfig ="/config/wifi_config";
  const char* mqttConfig = "/config/mqtt_config";
  const char* factoryReset = "/config/factory_reset";
  const char* firmwareUpgrade = "/ota_update";
};


Form form;
TimerMs MessageTimer, ServiceMessageTimer, handleTimerDelay;
EspMQTTClient mqttClient;
ESPRelay Relay1;


// Определение NTP-клиента для получения времени
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void updateConfig();
void readConfig();
void publishRelay();
void SendDiscoveryMessage();
void SendAvailableMessage(const String &mode );
void mqttPublish();
void factoryReset();
void ChangeRelayState();
void mqttStart();
void restart();
void println(const String& text);
void print(const String& text);


#include <webface.h>
#include <function.h>
#include <mqtt.h>



void setup() {
  startup();
}


void loop(){
  ArduinoOTA.handle();
  db.tick();
  mqttClient.loop();
  mqttPublish();
  portal.tick();
  wifiLoop();
  timeClient.update();
  handleTimerDelay.tick();
}