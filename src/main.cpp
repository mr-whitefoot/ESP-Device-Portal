#include <EspMQTTClient.h>
#include <ArduinoJson.h>
#include <TimerMs.h>
#include <GyverPortal.h>
#include <ESPRelay.h>
#include <Timezone.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <settings.h>
#include <settings_string.h>
#include <timezone_table.h>
#include <mqtt_topics.h>
#include <mqtt_payload.h>
#include <timer_schedule.h>
#include <keys.h>


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


#include <wifi_func.h>

// Библиотека WiFi адресует базу собственными ключами через SH(). Слой
// настроек считает хэш тем же алгоритмом, поэтому имена сходятся байт в байт
// и обе стороны видят одни и те же ячейки. Проверяем это на сборке, а не
// надеемся: разъехавшийся хэш означал бы, что портал правит одни настройки,
// а подключается библиотека по другим.
static_assert(keys::wifi::ssid.id == wifi::ssid, "ключ SSID разошёлся с wifi-библиотекой");
static_assert(keys::wifi::password.id == wifi::password, "ключ пароля разошёлся с wifi-библиотекой");
static_assert(keys::wifi::forceAP.id == wifi::forceAP, "ключ forceAP разошёлся с wifi-библиотекой");


#define RELAY_PIN 0


// Зеркала настроек в оперативке (прежняя struct Data) больше нет: каждый
// параметр читается из слоя там, где нужен. Именно зеркало было точкой
// связывания -- добавление устройства требовало правки общей структуры,
// а расхождение копии с базой уже приводило к багу с forceAP.
GyverPortal portal;

// Единственное исключение: расписание таймеров держится в памяти, потому что
// timerHandle() читает его раз в секунду и лазить за 25 ячейками на каждый
// тик незачем. Это кэш с одной точкой загрузки (timersLoad), а не зеркало.
Timers timers;
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

void timersLoad();
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
  settings::tick();
  mqttClient.loop();
  mqttPublish();
  portal.tick();
  wifiLoop();
  timeClient.update();
  handleTimerDelay.tick();
}