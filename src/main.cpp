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


//#define DEBUG_MQTT
//#define DEBUG_DB


String sw_version = "3.0.9";
String release_date = "10.03.2026";

GyverDBFile db(&LittleFS, "/data.db");


#include <wifi_func.h>


#define LIGHT_THEME 0
#define DARK_THEME 1
#define RELAY_PIN 0
#define TIMER_COUNT 5


struct Timer
{
  bool enable;
  byte action;
  byte hours;
  byte minutes;
  byte seconds;
};

struct Timers{
  Timer timer[TIMER_COUNT];
};

enum keys : size_t {
  deviceName = SH("deviceName"),
  
  relayInvertMode = SH("relayInvertMode"),
  saveRelayStatus = SH("saveRelayStatus"),
  relayState      = SH("relayState"),
  timezone        = SH("timezone"),

  theme = SH("theme"),
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
  Timers timers; 
  Timer timer; 
};


Data data;
GyverPortal portal;
GPlog glog("log");
WiFiEventHandler onSoftAPModeStationConnected, onSoftAPModeStationDisconnected, onStationModeConnected;


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

void publishRelay();
void SendDiscoveryMessage();
void SendAvailableMessage(const String &mode );
void mqttPublish();
void factoryReset();
void ChangeRelayState();
void mqttStart();
void restart();
int convertTimezoneToOffset(byte timezone);
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
  mqttClient.loop();
  mqttPublish();
  portal.tick();
  wifiLoop();
  timeClient.update();
  handleTimerDelay.tick();
}