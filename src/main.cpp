#include <ArduinoOTA.h>
#include <mqtt_client.h>
#include <ArduinoJson.h>
#include <TimerMs.h>
#include <GyverPortal.h>
#include <settings.h>
#include <settings_string.h>
#include <timezone_table.h>
#include <mqtt_topics.h>
#include <core_keys.h>
#include <core_contract.h>


//#define DEBUG_DB


// Версия приходит из platformio.ini через -DVERSION, дата сборки -- из
// extra_script.py через -DRELEASE_DATE. Так они не разъезжаются ни с
// release_version, ни с именем bin-файла, ни с реальностью.
// Макросы подставляются без кавычек, поэтому их нужно превратить в строку.
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

String sw_version = STRINGIFY(VERSION);
String release_date = STRINGIFY(RELEASE_DATE);


// Зеркала настроек в оперативке (прежняя struct Data) больше нет: каждый
// параметр читается из слоя там, где нужен. Именно зеркало было точкой
// связывания -- добавление устройства требовало правки общей структуры,
// а расхождение копии с базой уже приводило к багу с forceAP.
GyverPortal portal;
GPlog glog("log");


struct Form{
  const char* root = "/";
  const char* log = "/log";
  const char* config = "/config";
  const char* preferences = "/config/preferences";
  const char* WiFiConfig ="/config/wifi_config";
  const char* mqttConfig = "/config/mqtt_config";
  const char* factoryReset = "/config/factory_reset";
  const char* firmwareUpgrade = "/ota_update";
};


Form form;
TimerMs MessageTimer, ServiceMessageTimer, CleanupTimer, RediscoverTimer;
CoreMqttClient mqttClient;

void publishState();
void SendDiscoveryMessage();
void SendAvailableMessage(const String &mode );
void mqttPublish();
void mqttClearRetained();
void factoryReset();
void mqttStart();
void onConnectionEstablished();
void restart();
void restartRequest(const char* reason);


// Ядро сначала, устройство после: ядро зовёт device::*, объявленные в
// контракте выше, а определения приходят с реализацией устройства.
//
// core_log.h первым: он пользуется glog, объявленным выше, а его макросами
// пользуются все остальные.
#include <core_log.h>
#include <core_metrics.h>
#include <core_wifi.h>
// После core_wifi.h: адаптеру нужен WiFi.status(), а объявления сети приходят
// оттуда. До core_portal.h и core_boot.h: они уже зовут corentp::.
#include <core_ntp.h>
#include <core_portal.h>
#include <core_boot.h>
#include <core_mqtt.h>

// Устройство выбирается окружением сборки, тем же приёмом, что и бэкенд
// настроек в settings.h. Значения по умолчанию нет намеренно: собранная
// не тем устройством прошивка молча уедет на железку и щёлкнет чем попало.
#if defined(DEVICE_RELAY)
  #include <device_relay.h>
#elif defined(DEVICE_DS18B20)
  #include <device_ds18b20.h>
#else
  #error "Не выбрано устройство: соберите окружение с -D DEVICE_RELAY или -D DEVICE_DS18B20"
#endif



void setup() {
  startup();
}


void loop(){
  LOOP_METRICS_BEGIN();
  STAGE(ST_OTA,      ArduinoOTA.handle());
  STAGE(ST_SETTINGS, settings::tick());
  STAGE(ST_MQTT,     mqttClient.loop());
  STAGE(ST_PUBLISH,  mqttPublish());
  STAGE(ST_PORTAL,   portal.tick());
  STAGE(ST_WIFI,     corewifi::tick());
  STAGE(ST_NTP,      ntpTick());
  STAGE(ST_DEVICE,   device::tick());
  // Последней: отложенная перезагрузка обязана случиться после того, как
  // portal.tick() отдал ответ на форму. Отдельного этапа метрик у неё нет --
  // это сравнение двух чисел, а когда оно срабатывает, мерить уже некому.
  restartTick();
  LOOP_METRICS_END();
}
