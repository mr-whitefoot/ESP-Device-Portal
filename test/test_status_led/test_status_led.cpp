#include <unity.h>
#include <Arduino.h>
#include <status_led.h>

#define LED_PIN 13

void setUp(void) { resetArduinoFake(); }
void tearDown(void) {}

// Уровень, который физически зажигает светодиод.
static int litLevel(bool activeLow) { return activeLow ? LOW : HIGH; }

static bool everLit(bool activeLow) {
  for (const PinWrite& w : pinWrites())
    if (w.pin == LED_PIN && w.level == litLevel(activeLow)) return true;
  return false;
}

static int lastLevel() {
  return pinWrites().empty() ? -1 : pinWrites().back().level;
}

// Объект глобальный, то есть создаётся до setup(): всё, что он успел
// записать в пин, горит до конца загрузки.
void test_constructor_does_not_touch_pin(void) {
  StatusLed led;
  TEST_ASSERT_EQUAL_MESSAGE(0, pinWrites().size(), "конструктор пишет в пин");
  TEST_ASSERT_EQUAL_MESSAGE(0, pinModeCalls().size(), "конструктор зовёт pinMode");
}

// Тот же класс ошибки, что и у реле: защёлка выхода после сброса нулевая,
// а этот светодиод горит от LOW. pinMode(OUTPUT) без предварительной записи
// зажёг бы его на всю загрузку.
void test_boot_never_lights_active_low_led(void) {
  StatusLed led;
  led.begin(LED_PIN, true, false);

  TEST_ASSERT_FALSE_MESSAGE(everLit(true), "светодиод загорался при инициализации");
  TEST_ASSERT_FALSE(led.Get());
}

void test_boot_never_lights_active_high_led(void) {
  StatusLed led;
  led.begin(LED_PIN, false, false);

  TEST_ASSERT_FALSE_MESSAGE(everLit(false), "светодиод загорался при инициализации");
}

// Реле, поднявшееся во включённом состоянии, не должно оставить светодиод
// тёмным до первой команды.
void test_begin_can_start_lit(void) {
  StatusLed led;
  led.begin(LED_PIN, true, true);

  TEST_ASSERT_TRUE(led.Get());
  TEST_ASSERT_EQUAL_MESSAGE(LOW, lastLevel(), "включённый светодиод не зажёгся");
}

void test_begin_configures_output(void) {
  StatusLed led;
  led.begin(LED_PIN, true, false);

  TEST_ASSERT_EQUAL(1, pinModeCalls().size());
  TEST_ASSERT_EQUAL(LED_PIN, pinModeCalls()[0]);
}

void test_set_respects_active_low(void) {
  StatusLed led;
  led.begin(LED_PIN, true, false);

  led.Set(true);
  TEST_ASSERT_EQUAL_MESSAGE(LOW, lastLevel(), "включение не дало LOW");
  TEST_ASSERT_TRUE(led.Get());

  led.Set(false);
  TEST_ASSERT_EQUAL_MESSAGE(HIGH, lastLevel(), "выключение не дало HIGH");
  TEST_ASSERT_FALSE(led.Get());
}

void test_set_respects_active_high(void) {
  StatusLed led;
  led.begin(LED_PIN, false, false);

  led.Set(true);
  TEST_ASSERT_EQUAL(HIGH, lastLevel());

  led.Set(false);
  TEST_ASSERT_EQUAL(LOW, lastLevel());
}

// Мигание зовёт Set() с тем же значением по многу раз подряд: это должно
// оставаться безобидным.
void test_repeated_set_is_stable(void) {
  StatusLed led;
  led.begin(LED_PIN, true, false);

  led.Set(true);
  led.Set(true);
  TEST_ASSERT_EQUAL(LOW, lastLevel());
  TEST_ASSERT_TRUE(led.Get());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_constructor_does_not_touch_pin);
  RUN_TEST(test_boot_never_lights_active_low_led);
  RUN_TEST(test_boot_never_lights_active_high_led);
  RUN_TEST(test_begin_can_start_lit);
  RUN_TEST(test_begin_configures_output);
  RUN_TEST(test_set_respects_active_low);
  RUN_TEST(test_set_respects_active_high);
  RUN_TEST(test_repeated_set_is_stable);
  return UNITY_END();
}
