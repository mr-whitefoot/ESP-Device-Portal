#include <unity.h>
#include <mqtt_topics.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const char* sanitized(const char* src) {
  static char buf[64];
  sanitizeTopicSegment(src, buf, sizeof(buf));
  return buf;
}

// Имя по умолчанию содержит пробел, и именно из-за него HomeAssistant
// не принимал discovery-конфиг.
void test_default_device_name(void) {
  TEST_ASSERT_EQUAL_STRING("ESP_Relay", sanitized("ESP Relay"));
}

void test_already_valid_name_is_untouched(void) {
  TEST_ASSERT_EQUAL_STRING("ESPRelay", sanitized("ESPRelay"));
  TEST_ASSERT_EQUAL_STRING("kitchen-switch_1", sanitized("kitchen-switch_1"));
  TEST_ASSERT_EQUAL_STRING("Relay2", sanitized("Relay2"));
}

void test_mqtt_wildcards_and_separators_are_replaced(void) {
  TEST_ASSERT_EQUAL_STRING("a_b", sanitized("a/b"));
  TEST_ASSERT_EQUAL_STRING("a_b", sanitized("a+b"));
  TEST_ASSERT_EQUAL_STRING("a_b", sanitized("a#b"));
  TEST_ASSERT_EQUAL_STRING("AA_BB_CC", sanitized("AA:BB:CC"));
}

void test_repeated_replacements_collapse(void) {
  TEST_ASSERT_EQUAL_STRING("a_b", sanitized("a    b"));
  TEST_ASSERT_EQUAL_STRING("a_b", sanitized("a . , b"));
}

// Пустое имя дало бы в топике "//" и битый discovery.
void test_empty_and_unusable_names_fall_back(void) {
  TEST_ASSERT_EQUAL_STRING("ESP", sanitized(""));
  TEST_ASSERT_EQUAL_STRING("ESP", sanitized("   "));
  TEST_ASSERT_EQUAL_STRING("ESP", sanitized("реле"));
  TEST_ASSERT_EQUAL_STRING("ESP", sanitized(nullptr));
}

void test_result_never_contains_forbidden_chars(void) {
  const char* names[] = {"ESP Relay", "кухня/свет", "a+b#c", "!!!", "Дом 2", "x"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    const char* out = sanitized(names[i]);
    TEST_ASSERT_TRUE_MESSAGE(strlen(out) > 0, names[i]);
    for (const char* p = out; *p; ++p)
      TEST_ASSERT_TRUE_MESSAGE(isAllowedTopicChar(*p), names[i]);
  }
}

void test_does_not_overflow_small_buffer(void) {
  char buf[8];
  memset(buf, 'X', sizeof(buf));
  sanitizeTopicSegment("very long device name here", buf, sizeof(buf));

  TEST_ASSERT_EQUAL(7, strlen(buf));
  TEST_ASSERT_EQUAL_STRING("very_lo", buf);
}

void test_tiny_buffers_are_safe(void) {
  char one[1] = {'X'};
  sanitizeTopicSegment("name", one, sizeof(one));
  TEST_ASSERT_EQUAL_STRING("", one);

  char zero[1] = {'X'};
  sanitizeTopicSegment("name", zero, 0);
  TEST_ASSERT_EQUAL('X', zero[0]);  // буфер не тронут
}

// --- uniq_id --------------------------------------------------------------

static const char* uniqueId(uint32_t chipId, const char* component) {
  static char buf[32];
  buildUniqueId(chipId, component, buf, sizeof(buf));
  return buf;
}

// Раньше uniq_id был голым chip ID, то есть одинаковым у всех сущностей
// одного чипа. Компонент в суффиксе разводит их заранее.
void test_unique_id_carries_component(void) {
  TEST_ASSERT_EQUAL_STRING("4d2197_switch", uniqueId(0x4d2197, "switch"));
  TEST_ASSERT_EQUAL_STRING("48fc73_sensor", uniqueId(0x48fc73, "sensor"));
}

void test_unique_id_differs_between_entities_of_one_chip(void) {
  char sw[32], sens[32];
  buildUniqueId(0x4d2197, "switch", sw, sizeof(sw));
  buildUniqueId(0x4d2197, "sensor", sens, sizeof(sens));
  TEST_ASSERT_TRUE_MESSAGE(strcmp(sw, sens) != 0,
                           "две сущности одного чипа получили один uniq_id");
}

// Chip ID выравнивается нулями: иначе идентификаторы двух железок выглядели
// бы разной длины, а младшие чипы теряли бы ведущий ноль.
void test_unique_id_pads_chip_id(void) {
  TEST_ASSERT_EQUAL_STRING("000abc_switch", uniqueId(0xabc, "switch"));
  TEST_ASSERT_EQUAL_STRING("ffffff_sensor", uniqueId(0xffffff, "sensor"));
}

void test_unique_id_is_safe_with_tiny_buffer(void) {
  char buf[4];
  buildUniqueId(0x4d2197, "switch", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("4d2", buf);

  char zero[1] = {'X'};
  buildUniqueId(0x4d2197, "switch", zero, 0);
  TEST_ASSERT_EQUAL('X', zero[0]);  // буфер не тронут
}

void test_multi_entity_unique_id_carries_stable_entity_id(void) {
  char first[48], last[48];
  buildEntityUniqueId(0x4d2197, "switch", "relay_1", first, sizeof(first));
  buildEntityUniqueId(0x4d2197, "switch", "relay_8", last, sizeof(last));

  TEST_ASSERT_EQUAL_STRING("4d2197_switch_relay_1", first);
  TEST_ASSERT_EQUAL_STRING("4d2197_switch_relay_8", last);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(first, last));
}

void test_multi_entity_unique_id_handles_null_parts(void) {
  char buf[32];
  buildEntityUniqueId(0xabc, nullptr, nullptr, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("000abc__", buf);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_default_device_name);
  RUN_TEST(test_already_valid_name_is_untouched);
  RUN_TEST(test_mqtt_wildcards_and_separators_are_replaced);
  RUN_TEST(test_repeated_replacements_collapse);
  RUN_TEST(test_empty_and_unusable_names_fall_back);
  RUN_TEST(test_result_never_contains_forbidden_chars);
  RUN_TEST(test_does_not_overflow_small_buffer);
  RUN_TEST(test_tiny_buffers_are_safe);
  RUN_TEST(test_unique_id_carries_component);
  RUN_TEST(test_unique_id_differs_between_entities_of_one_chip);
  RUN_TEST(test_unique_id_pads_chip_id);
  RUN_TEST(test_unique_id_is_safe_with_tiny_buffer);
  RUN_TEST(test_multi_entity_unique_id_carries_stable_entity_id);
  RUN_TEST(test_multi_entity_unique_id_handles_null_parts);
  return UNITY_END();
}
