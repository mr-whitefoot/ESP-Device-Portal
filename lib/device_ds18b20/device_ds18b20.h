#pragma once
#include <DS18B20.h>
#include <OneWire.h>
#include <ds18b20_keys.h>
#include <math.h>
#include <median_filter.h>
#include <settings.h>
#include <settings_string.h>
#include <TimerMs.h>

// Датчик температуры DS18B20: реализация контракта устройства.
//
// Управлять здесь нечем, поэтому половина контракта -- заглушки: команд нет,
// кликов нет, своих страниц нет. Ядро об этом не знает и обращается только
// через device::*.

// Единственный пин ESP-01, свободный при работающем UART.
#define ONE_WIRE_BUS 2

namespace device {
namespace detail {

// 12 бит -- шаг 0.0625 °C. Время преобразования по даташиту, худший случай.
static const uint8_t SENSOR_RESOLUTION = 12;
static const uint32_t CONVERSION_TIME = 750;

// Диапазон датчика по даташиту. Библиотека отдаёт ошибки обычными числами
// (-127 обрыв линии, -128 ошибка CRC), и диапазон отсеивает их сам.
static const float SENSOR_MIN_C = -55.0f;
static const float SENSOR_MAX_C = 125.0f;

// Девять значений, как в прежней прошивке: при опросе раз в 10 секунд окно
// покрывает полторы минуты и переживает одиночный сбой чтения.
static const uint8_t FILTER_WINDOW = 9;

// Знак градуса в UTF-8. Записан двумя литералами намеренно: шестнадцатеричная
// escape-последовательность жадная, и "\xb0C" компилятор прочтёт как один
// символ с кодом 0xB0C, а не как градус и букву.
static const char DEGREE_C[] = "\xc2\xb0" "C";

OneWire oneWire(ONE_WIRE_BUS);
DS18B20 sensor(&oneWire);
TimerMs readTimer;
MedianFilter<FILTER_WINDOW> filter(SENSOR_MIN_C, SENSOR_MAX_C);

// Датчик мог отсутствовать на шине в момент загрузки. Само чтение это
// переживает -- getTempC() каждый раз ищет адрес заново, -- но разрешение
// выставляется только в begin(). Досматриваем это по факту удачного чтения,
// а не попытками вслепую: поиск по шине стоит около 13 мс блокировки.
bool sensorReady = false;

// Идёт преобразование. Ждём его по часам, не опрашивая шину: время известно
// из даташита, а каждый опрос готовности -- это транзакция OneWire с
// запрещёнными прерываниями, то есть потерянные пакеты радио.
bool converting = false;
uint32_t requestedAt = 0;

// Чтобы отсутствующий датчик не писал в лог одно и то же каждые 10 секунд.
// Считаем при этом все неудачи подряд: одиночный сбой чтения на длинной линии
// -- дело обычное, а сотня подряд означает оторванный датчик, и различает их
// только счётчик. Печатается он при восстановлении, когда уже известен.
bool lastReadOk = true;
uint16_t failStreak = 0;

void readTimerSetup() {
  int32_t period = settings::getInt(keys::sensor::refresh);
  // Ноль в поле формы означал бы опрос на каждом проходе loop(), то есть
  // постоянно занятую шину и забитый лог.
  if (period < 1) period = 1;

  readTimer.stop();
  readTimer.setTime((uint32_t)period * 1000);
  readTimer.start();
}

void sensorSetup() {
  sensorReady = sensor.begin();
  if (!sensorReady) return;

  // Проверка CRC ценой чтения девяти байт вместо двух: без неё повреждённый
  // ответ шины превращается в правдоподобную температуру, с ней приходит
  // -128 и фильтр его отбрасывает.
  sensor.setConfig(DS18B20_CRC);
  sensor.setResolution(SENSOR_RESOLUTION);
}

void readFailed(float value) {
  if (failStreak < 0xFFFF) failStreak++;
  if (!lastReadOk) return;

  // Отвергнутое значение целиком: библиотека отдаёт ошибки обычными числами
  // (-127 обрыв линии, -128 ошибка CRC), и по ним отличается оторванный
  // датчик от наводки на линии.
  LOG_W(dev, String(F("sensor read failed value=")) + String(value, 1));
  lastReadOk = false;
}

void startRead() {
  sensor.requestTemperatures();
  converting = true;
  requestedAt = millis();
}

void finishRead() {
  converting = false;

  float value = sensor.getTempC();
  if (!filter.add(value)) {
    readFailed(value);
    return;
  }

  // Датчик отвечает. Если при загрузке его не было, самое время выставить
  // разрешение и проверку CRC -- поиск по шине сейчас заведомо удачный,
  // в отличие от попыток вслепую каждый цикл.
  if (!sensorReady) sensorSetup();

  if (!lastReadOk)
    LOG_I(dev, String(F("sensor read recovered failed=")) + failStreak);
  lastReadOk = true;
  failStreak = 0;
}

// Показание для портала. Пока достоверных чтений нет, показывать нечего:
// ноль означал бы 0 °C, а не "не знаю".
String displayValue() {
  if (!filter.ready()) return String("--");
  return String(filter.value(), 1) + " " + DEGREE_C;
}

}  // namespace detail

// --- Описание -------------------------------------------------------------

const char* model() { return "Temperature"; }
const char* haComponent() { return "sensor"; }
const char* updateIds() { return "temperature"; }

// --- Жизненный цикл -------------------------------------------------------

void defineSettings() {
  settings::defineString(keys::sensor::label, "Temperature");
  settings::defineInt(keys::sensor::refresh, 10);
}

void begin() {
  // Шину подтягивает внешний резистор, ножка контроллера остаётся входом.
  pinMode(ONE_WIRE_BUS, INPUT);

  detail::sensorSetup();
  LOG_I(dev, String(F("ds18b20 init bus=")) + ONE_WIRE_BUS +
             F(" period=") + settings::getInt(keys::sensor::refresh) + "s");

  // Не ошибка: датчик, подключённый после загрузки, подхватится первым же
  // удачным чтением. Но пустая карточка в HomeAssistant объясняется именно
  // этой строкой.
  if (!detail::sensorReady) LOG_W(dev, F("ds18b20 not found on the bus"));

  detail::readTimerSetup();

  // Первое чтение сразу, не дожидаясь периода опроса: иначе портал и
  // HomeAssistant первые десять секунд показывают пустоту.
  detail::startRead();
}

void tick() {
  if (detail::converting) {
    // Ожидание по часам, а не опросом шины. Прежний вариант звал
    // isConversionComplete() на каждом проходе loop() -- это тысячи
    // транзакций OneWire за одно преобразование, каждая с запрещёнными
    // прерываниями. Портал тормозил, DNS не успевал отвечать на запросы
    // captive portal, а клиенты отваливались от точки доступа.
    //
    // Вычитание беззнаковых переживает переполнение millis().
    if (millis() - detail::requestedAt < detail::CONVERSION_TIME) return;
    detail::finishRead();
    return;
  }

  if (detail::readTimer.tick()) detail::startRead();
}

// --- Портал ---------------------------------------------------------------

void buildHomeUi() {
  GP.BLOCK_TAB_BEGIN("Sensor");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL( settings::getStringValue(keys::sensor::label) );
      GP.LABEL(detail::displayValue(), "temperature");
    GP.BOX_END();
  GP.BLOCK_END();
}

// Своих страниц у датчика нет.
void buildHomeLinks() {}
bool buildPage(const String& uri) { return false; }

void buildSettingsUi() {
  GP.BLOCK_TAB_BEGIN("Sensor");
    GP.TEXT("tempLabel", "Temperature label",
            settings::getStringValue(keys::sensor::label)); GP.BREAK();
    GP.NUMBER("sensorRefresh", "Refresh time, sec",
              settings::getInt(keys::sensor::refresh)); GP.BREAK();
  GP.BLOCK_END();
}

void readSettingsForm() {
  settings::setString(keys::sensor::label, portal.getString("tempLabel").c_str());

  int32_t refresh = portal.getInt("sensorRefresh");
  if (refresh < 1) refresh = 1;
  if (refresh != settings::getInt(keys::sensor::refresh))
    LOG_I(dev, String(F("sensor period=")) + refresh + "s");
  settings::setInt(keys::sensor::refresh, refresh);

  detail::readTimerSetup();
}

// Форм и кликов у датчика тоже нет.
bool handleForm() { return false; }
void handleClick() {}

void updateUi() {
  // Именованная переменная обязательна: updateString берёт String по
  // неконстантной ссылке и временное значение к ней не привязывается.
  String value = detail::displayValue();
  portal.updateString("temperature", value);
}

// --- MQTT -----------------------------------------------------------------

void fillDiscovery(JsonDocument& doc) {
  doc["dev_cla"]      = "temperature";
  doc["stat_cla"]     = "measurement";
  doc["unit_of_meas"] = detail::DEGREE_C;
  // default('') на случай, когда достоверных чтений ещё нет и поля в
  // состоянии нет вовсе: HomeAssistant покажет unknown, а не выдуманный ноль.
  doc["val_tpl"]      = "{{ value_json.temperature | default('') }}";
  // Ни cmd_t, ни pl_on/pl_off: датчиком не управляют.
}

void fillState(JsonDocument& doc) {
  if (!detail::filter.ready()) return;
  // Шаг датчика 0.0625 °C, и медиана чётного набора добавляет ещё половину
  // разряда. Десятых долей достаточно, остальное -- шум в полезной нагрузке.
  doc["temperature"] = roundf(detail::filter.value() * 10.0f) / 10.0f;
}

// Команд у датчика нет.
void onCommand(const String& payload) {}

}  // namespace device
