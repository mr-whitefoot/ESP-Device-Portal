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

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_constructor_does_not_touch_pin);
  RUN_TEST(test_boot_never_activates_inverted_relay);
  RUN_TEST(test_boot_never_activates_normal_relay);
  RUN_TEST(test_begin_configures_pin);
  RUN_TEST(test_set_state_respects_invert_mode);
  RUN_TEST(test_reset_state_toggles);
  RUN_TEST(test_invert_mode_change_keeps_logical_state);
  RUN_TEST(test_state_change_without_callback_is_safe);
  RUN_TEST(test_callback_fires_on_state_change);
  return UNITY_END();
}
