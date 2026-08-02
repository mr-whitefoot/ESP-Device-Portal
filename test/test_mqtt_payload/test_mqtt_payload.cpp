#include <unity.h>
#include <mqtt_payload.h>

void setUp(void) {}
void tearDown(void) {}

// То, что реально публикует HomeAssistant по объявленным pl_on/pl_off.
void test_home_assistant_payloads(void) {
  TEST_ASSERT_TRUE(parseSwitchPayload("ON", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("OFF", true));
}

// Прежний ToBool понимал только эту форму.
void test_legacy_boolean_payloads(void) {
  TEST_ASSERT_TRUE(parseSwitchPayload("true", false));
  TEST_ASSERT_TRUE(parseSwitchPayload("True", false));
  TEST_ASSERT_TRUE(parseSwitchPayload("TRUE", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("false", true));
  TEST_ASSERT_FALSE(parseSwitchPayload("False", true));
}

void test_case_is_ignored(void) {
  TEST_ASSERT_TRUE(parseSwitchPayload("on", false));
  TEST_ASSERT_TRUE(parseSwitchPayload("On", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("off", true));
  TEST_ASSERT_FALSE(parseSwitchPayload("oFf", true));
}

void test_numeric_and_yes_no(void) {
  TEST_ASSERT_TRUE(parseSwitchPayload("1", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("0", true));
  TEST_ASSERT_TRUE(parseSwitchPayload("yes", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("no", true));
}

// Неизвестная команда не должна дёргать нагрузку: раньше любая строка,
// кроме true, выключала реле.
void test_unknown_payload_keeps_current_state(void) {
  TEST_ASSERT_TRUE(parseSwitchPayload("wat", true));
  TEST_ASSERT_FALSE(parseSwitchPayload("wat", false));
  TEST_ASSERT_TRUE(parseSwitchPayload("", true));
  TEST_ASSERT_TRUE(parseSwitchPayload(nullptr, true));
  TEST_ASSERT_FALSE(parseSwitchPayload(nullptr, false));
}

// Префикс не должен считаться совпадением.
void test_partial_matches_are_rejected(void) {
  TEST_ASSERT_FALSE(parseSwitchPayload("ONE", false));
  TEST_ASSERT_TRUE(parseSwitchPayload("OFFLINE", true));
  TEST_ASSERT_FALSE(parseSwitchPayload("O", false));
  TEST_ASSERT_FALSE(parseSwitchPayload("truex", false));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_home_assistant_payloads);
  RUN_TEST(test_legacy_boolean_payloads);
  RUN_TEST(test_case_is_ignored);
  RUN_TEST(test_numeric_and_yes_no);
  RUN_TEST(test_unknown_payload_keeps_current_state);
  RUN_TEST(test_partial_matches_are_rejected);
  return UNITY_END();
}
