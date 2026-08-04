#pragma once
#include <ESPRelay.h>
#include <TimerMs.h>
#include <mqtt_payload.h>
#include <relay_keys.h>
#include <settings.h>
#include <settings_string.h>
#include <timer_schedule.h>

// Реле: реализация контракта устройства.
//
// Всё, что знает про реле -- пин, инверсия, сохранение состояния, суточные
// таймеры, вид в HomeAssistant -- живёт здесь. Ядро об этом не знает ничего
// и обращается только через device::*.

#define RELAY_PIN 0

namespace device {
namespace detail {

ESPRelay relay;
TimerMs scheduleTick;
TimerScheduler scheduler;

// Расписание держится в памяти намеренно: проверка идёт раз в секунду, и
// лазить за 25 ячейками настроек на каждый тик незачем. Это кэш с одной
// точкой загрузки, а не зеркало базы.
Timers timers;

const char* pageTimers = "/timers";

void timersLoad() {
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    timers.timer[i].enable = settings::getBool(keys::timer::enable[i]);
    timers.timer[i].action = (uint8_t)settings::getInt(keys::timer::action[i]);
    timers.timer[i].hours = (uint8_t)settings::getInt(keys::timer::hours[i]);
    timers.timer[i].minutes = (uint8_t)settings::getInt(keys::timer::minutes[i]);
    timers.timer[i].seconds = (uint8_t)settings::getInt(keys::timer::seconds[i]);
  }
}

void onRelayChanged() {
  println("Change relay state triggered");

  // В Button mode состояния ON живут всего полсекунды. Сохранять каждую
  // пару ON/OFF во флеш бессмысленно и вредно для его ресурса.
  if (settings::getBool(keys::relay::saveState) &&
      !settings::getBool(keys::relay::buttonMode)) {
    bool state = relay.GetState();

    // Пишем сразу, а не откладываем до settings::tick(). Отложенная запись
    // экономила ресурс флеша, но GyverDBFile сбрасывает файл только через
    // 10 секунд после изменения, и снятие питания в этом окне теряло
    // состояние -- ровно то, ради чего настройка и существует.
    //
    // Сравнение с сохранённым осталось, хотя лишние вызовы колбэка теперь
    // отсекает сам ESPRelay::SetState(). Оно ловит другой случай: состояние
    // менялось при выключенном "Save relay status", и в базе лежит устаревшее.
    if (settings::getBool(keys::relay::state) != state) {
      settings::setBool(keys::relay::state, state);
      settings::commit();
    }
  }

  publishState();
}

void scheduleHandle() {
  // До первой синхронизации NTPClient отсчитывает время от нуля, то есть
  // отдаёт 00:00:xx: таймер на начало суток срабатывал бы сразу после
  // включения. Заодно сбрасываем точку отсчёта, чтобы момент синхронизации
  // не выглядел скачком часов.
  if (!corentp::isTimeSet()) {
    scheduler.resync();
    return;
  }

  uint32_t now = (uint32_t)corentp::hours() * 3600UL +
                 (uint32_t)corentp::minutes() * 60UL +
                 (uint32_t)corentp::seconds();

  uint32_t due = scheduler.due(timers, now);
  if (!due) return;

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    if (!(due & (1UL << i))) continue;

    println("Timer " + String(i) + " activating");
    switch (timers.timer[i].action) {
      case TIMER_ACTION_ON:     relay.SetState(true);  break;
      case TIMER_ACTION_OFF:    relay.SetState(false); break;
      case TIMER_ACTION_TOGGLE: relay.ResetState();    break;
    }
  }
}

void buildTimerUi(const int index) {
  GP.BLOCK_TAB_BEGIN("Timer");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Timer"); GP.SWITCH("timerEnable"+String(index), timers.timer[index].enable);
    GP.BOX_END();
    GP.SELECT("timerHours"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23", timers.timer[index].hours);
    GP.SELECT("timerMinutes"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",timers.timer[index].minutes);
    GP.SELECT("timerSeconds"+String(index),"00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",timers.timer[index].seconds);
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Action"); GP.SELECT("timerAction"+String(index), "On,Off,Toggle", timers.timer[index].action);
    GP.BOX_END();
  GP.BLOCK_END();
}

void saveTimer(const int index) {
  bool enable = false;
  int action = 0, hours = 0, minutes = 0, seconds = 0;

  portal.copyBool("timerEnable"+String(index), enable);
  portal.copyInt("timerAction"+String(index), action);
  portal.copyInt("timerHours"+String(index), hours);
  portal.copyInt("timerMinutes"+String(index), minutes);
  portal.copyInt("timerSeconds"+String(index), seconds);

  settings::setBool(keys::timer::enable[index], enable);
  settings::setInt(keys::timer::action[index], action);
  settings::setInt(keys::timer::hours[index], hours);
  settings::setInt(keys::timer::minutes[index], minutes);
  settings::setInt(keys::timer::seconds[index], seconds);
}

}  // namespace detail

// --- Описание -------------------------------------------------------------

const char* model() { return "Relay"; }
const char* haComponent() { return "switch"; }
const char* updateIds() { return "switch"; }

// --- Жизненный цикл -------------------------------------------------------

void defineSettings() {
  settings::defineBool(keys::relay::invert, false);
  settings::defineBool(keys::relay::buttonMode, false);
  settings::defineBool(keys::relay::saveState, false);
  settings::defineBool(keys::relay::state, false);

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    settings::defineBool(keys::timer::enable[i], false);
    settings::defineInt(keys::timer::action[i], TIMER_ACTION_ON);
    settings::defineInt(keys::timer::hours[i], 0);
    settings::defineInt(keys::timer::minutes[i], 0);
    settings::defineInt(keys::timer::seconds[i], 0);
  }
}

void begin() {
  // Сохранённое состояние передаётся сразу в begin(): реле поднимается в
  // нужном положении одним движением, без промежуточного "выключено".
  bool buttonMode = settings::getBool(keys::relay::buttonMode);
  bool restored = !buttonMode &&
                  settings::getBool(keys::relay::saveState) &&
                  settings::getBool(keys::relay::state);
  if (restored) println("Restore relay state: on");

  detail::relay.begin(RELAY_PIN, settings::getBool(keys::relay::invert),
                      restored, buttonMode);
  detail::relay.ChangeStateCallback(detail::onRelayChanged);

  detail::timersLoad();

  println("Starting timers handler");
  detail::scheduleTick.setTime(1000);
  detail::scheduleTick.attach(detail::scheduleHandle);
  detail::scheduleTick.start();
}

void tick() {
  detail::relay.Tick();
  detail::scheduleTick.tick();
}

// --- Портал ---------------------------------------------------------------

void buildHomeUi() {
  GP.BLOCK_TAB_BEGIN("Control");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL( settings::getStringValue(keys::dev::name) );
      GP.SWITCH("switch", detail::relay.GetState());
    GP.BOX_END();
  GP.BLOCK_END();
}

void buildHomeLinks() {
  GP.BUTTON_LINK(detail::pageTimers, "Timers");
}

bool buildPage(const String& uri) {
  if (uri != detail::pageTimers) return false;

  GP.FORM_BEGIN(detail::pageTimers);
    GP.PAGE_TITLE("Timers");
    GP.TITLE("Timers");
    GP.HR();
    for (int i = 0; i < TIMER_COUNT; i++) detail::buildTimerUi(i);
    GP.HR();
    GP.SUBMIT("Save");
  GP.FORM_END();

  GP.BUTTON_LINK("/", "Back");
  return true;
}

void buildSettingsUi() {
  GP.BLOCK_TAB_BEGIN("Relay");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Relay invert mode");
      GP.SWITCH("relayInvertMode", settings::getBool(keys::relay::invert));
    GP.BOX_END();
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Button mode");
      GP.SWITCH("relayButtonMode", settings::getBool(keys::relay::buttonMode));
    GP.BOX_END();
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Save relay status");
      GP.SWITCH("relaySaveStatus", settings::getBool(keys::relay::saveState));
    GP.BOX_END();
  GP.BLOCK_END();
}

void readSettingsForm() {
  bool invert = portal.getCheck("relayInvertMode");
  settings::setBool(keys::relay::invert, invert);
  detail::relay.SetInvertMode(invert);

  bool buttonMode = portal.getCheck("relayButtonMode");
  settings::setBool(keys::relay::buttonMode, buttonMode);
  // Старое сохранённое ON не должно внезапно восстановиться, если Button
  // mode позднее выключат и перезагрузят устройство до новой команды.
  if (buttonMode) settings::setBool(keys::relay::state, false);
  detail::relay.SetButtonMode(buttonMode);

  settings::setBool(keys::relay::saveState, portal.getCheck("relaySaveStatus"));
}

bool handleForm() {
  if (!portal.form(detail::pageTimers)) return false;

  for (int i = 0; i < TIMER_COUNT; i++) detail::saveTimer(i);
  settings::commit();
  detail::timersLoad();  // обновить кэш расписания
  return true;
}

void handleClick() {
  if (portal.click("switch")) detail::relay.SetState( portal.getCheck("switch") );
}

void updateUi() {
  portal.updateInt("switch", detail::relay.GetState());
}

// --- MQTT -----------------------------------------------------------------

void fillDiscovery(JsonDocument& doc) {
  // Явные строки вместо булевых значений: раньше здесь лежали JSON true/false,
  // и работало это лишь по совпадению -- HomeAssistant приводит их к строкам
  // "True"/"False" ровно так же, как Jinja рендерит булево в шаблоне.
  doc["stat_on"]  = "ON";
  doc["stat_off"] = "OFF";
  doc["cmd_t"]    = getCommandTopic();
  doc["pl_on"]    = "ON";
  doc["pl_off"]   = "OFF";
  doc["dev_cla"]  = "switch";
  doc["val_tpl"]  = "{{ 'ON' if value_json.switch else 'OFF' }}";
}

void fillState(JsonDocument& doc) {
  doc["switch"] = detail::relay.GetState();
}

void onCommand(const String& payload) {
  // Нераспознанная команда не должна дёргать нагрузку, поэтому запасной
  // вариант -- текущее состояние.
  detail::relay.SetState( parseSwitchPayload(payload.c_str(), detail::relay.GetState()) );
}

}  // namespace device
