#include <unity.h>
#include <Arduino.h>
#include <ESPRelay.h>

#define RELAY_PIN 0

void setUp(void) { resetArduinoFake(); }
void tearDown(void) {}

// Уровень на пине, который физически включает реле.
static int activeLevel(bool invertMode) { return invertMode ? LOW : HIGH; }

static bool everDroveActive(bool invertMode) {
  for (const PinWrite& w : pinWrites())
    if (w.pin == RELAY_PIN && w.level == activeLevel(invertMode)) return true;
  return false;
}

// Объект реле глобальный, то есть конструируется до setup(). Любая запись
// в пин отсюда живёт до конца загрузки: Serial, LittleFS, чтение базы.
void test_constructor_does_not_touch_pin(void) {
  ESPRelay relay;
  TEST_ASSERT_EQUAL_MESSAGE(0, pinWrites().size(), "конструктор пишет в пин");
  TEST_ASSERT_EQUAL_MESSAGE(0, pinModeCalls().size(), "конструктор зовёт pinMode");
}

// Ядро бага: инверсия применялась отдельным вызовом уже после того,
// как пин был прижат к LOW, и реле щёлкало при каждой загрузке.
void test_boot_never_activates_inverted_relay(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true);

  TEST_ASSERT_FALSE_MESSAGE(everDroveActive(true), "реле включалось в ходе инициализации");
  TEST_ASSERT_FALSE(relay.GetState());
}

void test_boot_never_activates_normal_relay(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);

  TEST_ASSERT_FALSE_MESSAGE(everDroveActive(false), "реле включалось в ходе инициализации");
  TEST_ASSERT_FALSE(relay.GetState());
}

// Уровень обязан попасть в защёлку ДО pinMode(OUTPUT): иначе ножка выходит
// с нулём из сброшенного регистра, и на плате с инверсией реле щёлкает.
void test_level_is_set_before_pin_becomes_output(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true);

  TEST_ASSERT_TRUE_MESSAGE(pinWrites().size() > 0, "уровень не выставлен вовсе");
  TEST_ASSERT_EQUAL_MESSAGE(HIGH, pinWrites()[0].level,
                            "первым делом должен уйти неактивный уровень");
  TEST_ASSERT_EQUAL_MESSAGE(1, pinModeCalls().size(), "pinMode вызван не один раз");
}

// Реле с сохранённым включённым состоянием обязано подняться включённым
// одним движением, без промежуточного выключения.
void test_restores_saved_on_state_without_toggling(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true, true);

  TEST_ASSERT_TRUE(relay.GetState());
  for (const PinWrite& w : pinWrites())
    TEST_ASSERT_EQUAL_MESSAGE(LOW, w.level, "реле выключалось по дороге к включению");
}

void test_restores_saved_on_state_normal_mode(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false, true);

  TEST_ASSERT_TRUE(relay.GetState());
  for (const PinWrite& w : pinWrites())
    TEST_ASSERT_EQUAL_MESSAGE(HIGH, w.level, "реле выключалось по дороге к включению");
}

void test_begin_configures_pin(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);

  TEST_ASSERT_EQUAL(1, pinModeCalls().size());
  TEST_ASSERT_EQUAL(RELAY_PIN, pinModeCalls()[0]);
}

void test_set_state_respects_invert_mode(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true);
  relay.SetState(true);

  TEST_ASSERT_TRUE(relay.GetState());
  TEST_ASSERT_EQUAL(LOW, pinWrites().back().level);

  relay.SetState(false);
  TEST_ASSERT_FALSE(relay.GetState());
  TEST_ASSERT_EQUAL(HIGH, pinWrites().back().level);
}

void test_reset_state_toggles(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);

  relay.ResetState();
  TEST_ASSERT_TRUE(relay.GetState());
  relay.ResetState();
  TEST_ASSERT_FALSE(relay.GetState());
}

// Смена инверсии описывает железо, а не желание пользователя переключить
// нагрузку: логическое состояние обязано сохраниться, полярность выхода нет.
void test_invert_mode_change_keeps_logical_state(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);
  relay.SetState(true);
  TEST_ASSERT_EQUAL(HIGH, pinWrites().back().level);

  relay.SetInvertMode(true);
  TEST_ASSERT_TRUE_MESSAGE(relay.GetState(), "логическое состояние потерялось");
  TEST_ASSERT_EQUAL_MESSAGE(LOW, pinWrites().back().level, "полярность не переключилась");
}

static int callbackCalls = 0;
static void countCallback() { callbackCalls++; }

void test_state_change_without_callback_is_safe(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);
  relay.SetState(true);
  TEST_ASSERT_TRUE(relay.GetState());
}

void test_callback_fires_on_state_change(void) {
  ESPRelay relay;
  callbackCalls = 0;
  relay.ChangeStateCallback(countCallback);
  relay.SetState(true);
  relay.SetState(false);

  TEST_ASSERT_EQUAL(2, callbackCalls);
}

// Колбэк публикует состояние в MQTT и пишет флеш, поэтому на команду,
// ничего не меняющую, он дёргаться не должен. HomeAssistant повторяет
// команду при каждом нажатии, независимо от того, что показывает.
void test_callback_silent_on_repeated_state(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);
  callbackCalls = 0;
  relay.ChangeStateCallback(countCallback);

  relay.SetState(true);
  relay.SetState(true);
  TEST_ASSERT_EQUAL_MESSAGE(1, callbackCalls, "повторная команда дёрнула колбэк");

  relay.SetState(false);
  relay.SetState(false);
  TEST_ASSERT_EQUAL_MESSAGE(2, callbackCalls, "повторная команда дёрнула колбэк");
}

// Смена инверсии описывает железо: полярность выхода меняется, а состояние
// с точки зрения пользователя остаётся прежним -- публиковать нечего.
// На железе это вылезало так: сохранение формы Preferences зовёт
// SetInvertMode() и клало в брокер лишний state, причём в старый топик,
// если на той же форме сменили имя устройства.
void test_invert_mode_change_does_not_fire_callback(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);
  relay.SetState(true);

  callbackCalls = 0;
  relay.ChangeStateCallback(countCallback);
  relay.SetInvertMode(true);

  TEST_ASSERT_EQUAL_MESSAGE(0, callbackCalls, "смена инверсии опубликовала состояние");
  TEST_ASSERT_EQUAL_MESSAGE(LOW, pinWrites().back().level, "полярность не переключилась");
}

void test_button_mode_releases_after_half_second(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false, false, true);
  relay.SetState(true);

  TEST_ASSERT_TRUE(relay.GetState());
  TEST_ASSERT_EQUAL(HIGH, pinWrites().back().level);

  fakeMillis() = ESPRelay::BUTTON_PRESS_MS - 1;
  relay.Tick();
  TEST_ASSERT_TRUE_MESSAGE(relay.GetState(), "кнопка отпущена раньше 500 мс");

  fakeMillis() = ESPRelay::BUTTON_PRESS_MS;
  relay.Tick();
  TEST_ASSERT_FALSE_MESSAGE(relay.GetState(), "кнопка не отпущена через 500 мс");
  TEST_ASSERT_EQUAL(LOW, pinWrites().back().level);
}

void test_button_mode_respects_invert_mode(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true, false, true);
  relay.SetState(true);
  TEST_ASSERT_EQUAL(LOW, pinWrites().back().level);

  fakeMillis() = ESPRelay::BUTTON_PRESS_MS;
  relay.Tick();
  TEST_ASSERT_EQUAL(HIGH, pinWrites().back().level);
}

void test_repeated_button_press_restarts_hold_time(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false, false, true);
  relay.SetState(true);

  fakeMillis() = 400;
  relay.SetState(true);
  fakeMillis() = 899;
  relay.Tick();
  TEST_ASSERT_TRUE_MESSAGE(relay.GetState(), "повторное нажатие не продлило импульс");

  fakeMillis() = 900;
  relay.Tick();
  TEST_ASSERT_FALSE(relay.GetState());
}

void test_enabling_button_mode_releases_latched_relay(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false);
  relay.SetState(true);
  relay.SetButtonMode(true);

  TEST_ASSERT_TRUE(relay.GetButtonMode());
  TEST_ASSERT_FALSE_MESSAGE(relay.GetState(), "реле осталось защёлкнутым");
  TEST_ASSERT_EQUAL(LOW, pinWrites().back().level);
}

void test_button_mode_does_not_restore_saved_on_state(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, true, true, true);

  TEST_ASSERT_FALSE_MESSAGE(relay.GetState(), "Button mode восстановил нажатие");
  TEST_ASSERT_FALSE_MESSAGE(everDroveActive(true), "реле включилось при загрузке");
}

void test_button_timeout_survives_millis_overflow(void) {
  ESPRelay relay;
  relay.begin(RELAY_PIN, false, false, true);
  fakeMillis() = 0xFFFFFF00UL;
  relay.SetState(true);

  fakeMillis() = 0x000000F3UL;  // 499 мс после нажатия через переполнение
  relay.Tick();
  TEST_ASSERT_TRUE(relay.GetState());

  fakeMillis() = 0x000000F4UL;  // ровно 500 мс
  relay.Tick();
  TEST_ASSERT_FALSE(relay.GetState());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_constructor_does_not_touch_pin);
  RUN_TEST(test_boot_never_activates_inverted_relay);
  RUN_TEST(test_boot_never_activates_normal_relay);
  RUN_TEST(test_level_is_set_before_pin_becomes_output);
  RUN_TEST(test_restores_saved_on_state_without_toggling);
  RUN_TEST(test_restores_saved_on_state_normal_mode);
  RUN_TEST(test_begin_configures_pin);
  RUN_TEST(test_set_state_respects_invert_mode);
  RUN_TEST(test_reset_state_toggles);
  RUN_TEST(test_invert_mode_change_keeps_logical_state);
  RUN_TEST(test_state_change_without_callback_is_safe);
  RUN_TEST(test_callback_fires_on_state_change);
  RUN_TEST(test_callback_silent_on_repeated_state);
  RUN_TEST(test_invert_mode_change_does_not_fire_callback);
  RUN_TEST(test_button_mode_releases_after_half_second);
  RUN_TEST(test_button_mode_respects_invert_mode);
  RUN_TEST(test_repeated_button_press_restarts_hold_time);
  RUN_TEST(test_enabling_button_mode_releases_latched_relay);
  RUN_TEST(test_button_mode_does_not_restore_saved_on_state);
  RUN_TEST(test_button_timeout_survives_millis_overflow);
  return UNITY_END();
}
