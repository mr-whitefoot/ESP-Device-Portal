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
  return UNITY_END();
}
