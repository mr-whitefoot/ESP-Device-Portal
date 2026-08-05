#pragma once
#include <ESPRelay.h>
#include <TimerMs.h>
#include <mqtt_payload.h>
#include <relay_bank_keys.h>
#include <relay_bank_schedule.h>
#include <settings.h>
#include <settings_string.h>
#include <timer_schedule.h>

#if !defined(RELAY1_PIN) || !defined(RELAY2_PIN) || !defined(RELAY3_PIN) || \
    !defined(RELAY4_PIN) || !defined(RELAY5_PIN) || !defined(RELAY6_PIN) || \
    !defined(RELAY7_PIN) || !defined(RELAY8_PIN)
  #error "Для DEVICE_RELAY_BANK должны быть заданы RELAY1_PIN ... RELAY8_PIN"
#endif

namespace device {
namespace detail {

static const uint8_t RELAY_PINS[RELAY_BANK_COUNT] = {
    RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN,
    RELAY5_PIN, RELAY6_PIN, RELAY7_PIN, RELAY8_PIN,
};

ESPRelay relays[RELAY_BANK_COUNT];
bool observedState[RELAY_BANK_COUNT] = {};
TimerMs scheduleTick;
TimerScheduler scheduler;
Timers timers;
uint8_t timerTargets[TIMER_COUNT] = {};
const char* pageTimers = "/timers";

String indexedId(const char* prefix, uint8_t index) {
  return String(prefix) + index;
}

String relayOptions() {
  String options;
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    if (i) options += ',';
    String label = settings::getStringValue(keys::relayBank::label[i]);
    label.replace(",", " ");
    options += String(i + 1) + ": " + label;
  }
  return options;
}

void timersLoad() {
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    timerTargets[i] = relayBankTarget(settings::getInt(keys::bankTimer::target[i]));
    timers.timer[i].enable = settings::getBool(keys::bankTimer::enable[i]);
    timers.timer[i].action = settings::getBool(
        keys::relayBank::buttonMode[timerTargets[i]])
        ? TIMER_ACTION_ON
        : (uint8_t)settings::getInt(keys::bankTimer::action[i]);
    timers.timer[i].hours = (uint8_t)settings::getInt(keys::bankTimer::hours[i]);
    timers.timer[i].minutes = (uint8_t)settings::getInt(keys::bankTimer::minutes[i]);
    timers.timer[i].seconds = (uint8_t)settings::getInt(keys::bankTimer::seconds[i]);
  }
}

// Один проход собирает все изменения каналов в одну запись настроек и одну
// MQTT-публикацию. Это важно и для одновременного таймера, и для Button mode:
// восемь отдельных callback-ов создавали бы очередь одинаковых state JSON.
void syncStates() {
  bool changed = false;
  bool commit = false;

  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    bool state = relays[i].GetState();
    if (state == observedState[i]) continue;

    observedState[i] = state;
    changed = true;
    LOG_I(dev, String(F("relay=")) + (i + 1) + F(" state=") +
               (state ? "on" : "off"));

    if (settings::getBool(keys::relayBank::saveState[i]) &&
        !settings::getBool(keys::relayBank::buttonMode[i]) &&
        settings::getBool(keys::relayBank::state[i]) != state) {
      settings::setBool(keys::relayBank::state[i], state);
      commit = true;
    }
  }

  if (commit) settings::commit();
  if (changed) publishState();
}

void scheduleHandle() {
  if (!corentp::isTimeSet()) {
    scheduler.resync();
    return;
  }

  uint32_t now = (uint32_t)corentp::hours() * 3600UL +
                 (uint32_t)corentp::minutes() * 60UL +
                 (uint32_t)corentp::seconds();
  uint32_t due = scheduler.due(timers, now);

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    if (!(due & (1UL << i))) continue;

    uint8_t target = timerTargets[i];
    LOG_I(dev, String(F("timer=")) + i + F(" relay=") + (target + 1) +
               F(" action=") +
               (timers.timer[i].action == TIMER_ACTION_ON    ? "on" :
                timers.timer[i].action == TIMER_ACTION_OFF   ? "off" : "toggle"));
    switch (timers.timer[i].action) {
      case TIMER_ACTION_ON:     relays[target].SetState(true);  break;
      case TIMER_ACTION_OFF:    relays[target].SetState(false); break;
      case TIMER_ACTION_TOGGLE: relays[target].ResetState();    break;
    }
  }
}

void buildTimerUi(uint8_t index, const String& options) {
  uint8_t target = timerTargets[index];
  GP.BLOCK_TAB_BEGIN("Timer");
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Timer");
      GP.SWITCH(indexedId("bankTimerEnable", index), timers.timer[index].enable);
    GP.BOX_END();
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Relay");
      GP.SELECT(indexedId("bankTimerTarget", index), options, target);
    GP.BOX_END();
    GP.SELECT(indexedId("bankTimerHours", index),
              "00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23",
              timers.timer[index].hours);
    GP.SELECT(indexedId("bankTimerMinutes", index),
              "00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",
              timers.timer[index].minutes);
    GP.SELECT(indexedId("bankTimerSeconds", index),
              "00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59",
              timers.timer[index].seconds);
    GP.BOX_BEGIN(GP_EDGES);
      GP.LABEL("Action");
      GP.SELECT(indexedId("bankTimerAction", index), "On,Off,Toggle",
                timers.timer[index].action);
    GP.BOX_END();
    if (settings::getBool(keys::relayBank::buttonMode[target]))
      GP.LABEL("Button mode target always uses On");
  GP.BLOCK_END();
}

void saveTimer(uint8_t index) {
  bool enable = false;
  int target = 0, action = TIMER_ACTION_ON;
  int hours = 0, minutes = 0, seconds = 0;

  portal.copyBool(indexedId("bankTimerEnable", index), enable);
  portal.copyInt(indexedId("bankTimerTarget", index), target);
  portal.copyInt(indexedId("bankTimerAction", index), action);
  portal.copyInt(indexedId("bankTimerHours", index), hours);
  portal.copyInt(indexedId("bankTimerMinutes", index), minutes);
  portal.copyInt(indexedId("bankTimerSeconds", index), seconds);

  settings::setBool(keys::bankTimer::enable[index], enable);
  settings::setInt(keys::bankTimer::target[index], relayBankTarget(target));
  settings::setInt(keys::bankTimer::action[index], action);
  settings::setInt(keys::bankTimer::hours[index], hours);
  settings::setInt(keys::bankTimer::minutes[index], minutes);
  settings::setInt(keys::bankTimer::seconds[index], seconds);
}

}  // namespace detail

// --- Описание -------------------------------------------------------------

const char* model() { return "Relay Bank 8"; }
const char* haComponent() { return "switch"; }
uint8_t entityCount() { return RELAY_BANK_COUNT; }
const char* entityId(uint8_t index) {
  return index < RELAY_BANK_COUNT ? RELAY_BANK_ENTITY_IDS[index] : "relay_1";
}
void entityName(uint8_t index, char* buffer, size_t size) {
  if (index >= RELAY_BANK_COUNT) index = 0;
  settings::getString(keys::relayBank::label[index], buffer, size);
  if (size && !buffer[0]) snprintf(buffer, size, "Relay %u", index + 1);
}

const char* updateIds() {
  static String ids;
  ids = "";
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    if (settings::getBool(keys::relayBank::buttonMode[i])) continue;
    if (ids.length()) ids += ',';
    ids += detail::indexedId("bankSwitch", i);
  }
  return ids.c_str();
}

// --- Жизненный цикл -------------------------------------------------------

void defineSettings() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    char label[16];
    snprintf(label, sizeof(label), "Relay %u", i + 1);
    settings::defineString(keys::relayBank::label[i], label);
    settings::defineBool(keys::relayBank::invert[i], false);
    settings::defineBool(keys::relayBank::buttonMode[i], false);
    settings::defineBool(keys::relayBank::saveState[i], false);
    settings::defineBool(keys::relayBank::state[i], false);
  }

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    settings::defineBool(keys::bankTimer::enable[i], false);
    settings::defineInt(keys::bankTimer::target[i], 0);
    settings::defineInt(keys::bankTimer::action[i], TIMER_ACTION_ON);
    settings::defineInt(keys::bankTimer::hours[i], 0);
    settings::defineInt(keys::bankTimer::minutes[i], 0);
    settings::defineInt(keys::bankTimer::seconds[i], 0);
  }
}

void begin() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    bool buttonMode = settings::getBool(keys::relayBank::buttonMode[i]);
    bool restored = !buttonMode &&
                    settings::getBool(keys::relayBank::saveState[i]) &&
                    settings::getBool(keys::relayBank::state[i]);
    bool invert = settings::getBool(keys::relayBank::invert[i]);

    detail::relays[i].begin(detail::RELAY_PINS[i], invert, restored, buttonMode);
    detail::observedState[i] = detail::relays[i].GetState();
    LOG_I(dev, String(F("relay=")) + (i + 1) + F(" pin=") +
               detail::RELAY_PINS[i] + F(" invert=") +
               (invert ? "on" : "off") + F(" button=") +
               (buttonMode ? "on" : "off") + F(" state=") +
               (restored ? "on" : "off"));
  }

  detail::timersLoad();
  detail::scheduleTick.setTime(1000);
  detail::scheduleTick.attach(detail::scheduleHandle);
  detail::scheduleTick.start();
}

void tick() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) detail::relays[i].Tick();
  detail::scheduleTick.tick();
  detail::syncStates();
}

// --- Портал ---------------------------------------------------------------

void buildHomeUi() {
  GP.BLOCK_TAB_BEGIN("Control");
    for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
      bool buttonMode = settings::getBool(keys::relayBank::buttonMode[i]);
      GP.BOX_BEGIN(GP_EDGES);
        GP.LABEL(settings::getStringValue(keys::relayBank::label[i]));
        if (buttonMode)
          GP.BUTTON(detail::indexedId("bankPush", i), "Push");
        else
          GP.SWITCH(detail::indexedId("bankSwitch", i),
                    detail::relays[i].GetState());
      GP.BOX_END();
    }
  GP.BLOCK_END();
}

void buildHomeLinks() {
  GP.BUTTON_LINK(detail::pageTimers, "Timers");
}

bool buildPage(const String& uri) {
  if (uri != detail::pageTimers) return false;

  String options = detail::relayOptions();
  GP.FORM_BEGIN(detail::pageTimers);
    GP.PAGE_TITLE("Timers");
    GP.TITLE("Timers");
    GP.HR();
    for (uint8_t i = 0; i < TIMER_COUNT; i++)
      detail::buildTimerUi(i, options);
    GP.HR();
    GP.SUBMIT("Save");
  GP.FORM_END();
  GP.BUTTON_LINK("/", "Back");
  return true;
}

void buildSettingsUi() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    GP.BLOCK_TAB_BEGIN("Relay channel");
      GP.LABEL(String("Relay ") + (i + 1));
      GP.TEXT(detail::indexedId("bankLabel", i), "Label",
              settings::getStringValue(keys::relayBank::label[i])); GP.BREAK();
      GP.BOX_BEGIN(GP_EDGES);
        GP.LABEL("Invert mode");
        GP.SWITCH(detail::indexedId("bankInvert", i),
                  settings::getBool(keys::relayBank::invert[i]));
      GP.BOX_END();
      GP.BOX_BEGIN(GP_EDGES);
        GP.LABEL("Button mode");
        GP.SWITCH(detail::indexedId("bankButton", i),
                  settings::getBool(keys::relayBank::buttonMode[i]));
      GP.BOX_END();
      GP.BOX_BEGIN(GP_EDGES);
        GP.LABEL("Save relay status");
        GP.SWITCH(detail::indexedId("bankSave", i),
                  settings::getBool(keys::relayBank::saveState[i]));
      GP.BOX_END();
    GP.BLOCK_END();
  }
}

void readSettingsForm() {
  bool labelsChanged = false;

  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    String label = portal.getString(detail::indexedId("bankLabel", i));
    if (!label.length()) label = String("Relay ") + (i + 1);
    if (label != settings::getStringValue(keys::relayBank::label[i]))
      labelsChanged = true;
    settings::setString(keys::relayBank::label[i], label.c_str());

    bool invert = portal.getCheck(detail::indexedId("bankInvert", i));
    bool buttonMode = portal.getCheck(detail::indexedId("bankButton", i));
    settings::setBool(keys::relayBank::invert[i], invert);
    detail::relays[i].SetInvertMode(invert);

    settings::setBool(keys::relayBank::buttonMode[i], buttonMode);
    if (buttonMode) settings::setBool(keys::relayBank::state[i], false);
    detail::relays[i].SetButtonMode(buttonMode);

    bool saveState = portal.getCheck(detail::indexedId("bankSave", i));
    settings::setBool(keys::relayBank::saveState[i], saveState);
    // Включённое сохранение начинает действовать сразу: иначе текущее ON
    // потерялось бы при питании до первой последующей команды.
    if (saveState && !buttonMode)
      settings::setBool(keys::relayBank::state[i], detail::relays[i].GetState());
  }

  detail::timersLoad();
  if (labelsChanged && mqttClient.isConnected()) SendDiscoveryMessage();
}

bool handleForm() {
  if (!portal.form(detail::pageTimers)) return false;
  for (uint8_t i = 0; i < TIMER_COUNT; i++) detail::saveTimer(i);
  settings::commit();
  detail::timersLoad();
  return true;
}

void handleClick() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    if (portal.click(detail::indexedId("bankSwitch", i)))
      detail::relays[i].SetState(
          portal.getCheck(detail::indexedId("bankSwitch", i)));
    if (portal.click(detail::indexedId("bankPush", i)))
      detail::relays[i].SetState(true);
  }
}

void updateUi() {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    if (settings::getBool(keys::relayBank::buttonMode[i])) continue;
    portal.updateInt(detail::indexedId("bankSwitch", i),
                     detail::relays[i].GetState());
  }
}

// --- MQTT -----------------------------------------------------------------

void fillDiscovery(uint8_t entity, JsonDocument& doc) {
  doc["stat_on"] = "ON";
  doc["stat_off"] = "OFF";
  doc["cmd_t"] = getCommandTopic(entity);
  doc["pl_on"] = "ON";
  doc["pl_off"] = "OFF";
  doc["dev_cla"] = "switch";
  doc["val_tpl"] = String("{{ 'ON' if value_json.") + entityId(entity) +
                   " else 'OFF' }}";
}

void fillState(JsonDocument& doc) {
  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++)
    doc[RELAY_BANK_ENTITY_IDS[i]] = detail::relays[i].GetState();
}

void onCommand(uint8_t entity, const String& payload) {
  if (entity >= RELAY_BANK_COUNT) return;
  bool current = detail::relays[entity].GetState();
  detail::relays[entity].SetState(parseSwitchPayload(payload.c_str(), current));
}

}  // namespace device
