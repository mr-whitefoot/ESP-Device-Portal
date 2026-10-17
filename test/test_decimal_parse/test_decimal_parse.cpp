#include <decimal_parse.h>
#include <stdlib.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// Для поддерживаемой грамматики результат должен совпадать с libc. Сравниваем
// как float: именно во float значения уходят в настройки и на страницу.
static void same_as_libc(const char* s) {
  TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float)strtod(s, nullptr), (float)parseDecimal(s), s);
}

void test_matches_libc_on_portal_inputs(void) {
  const char* cases[] = {"-12.5", "0.0625", "125",  "  3.14abc", "",
                         "-",     ".5",     "5.",   "+7.25",     "007",
                         "\t42",  "-55",    "21.3", "36.6",      "100.3",
                         "0.1",   "0.3",    "-0.001", "1.23456789012345"};
  for (const char* c : cases) same_as_libc(c);
}

void test_negative_zero_is_zero_valued(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("-0"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("-0.0"));
}

void test_many_decimals(void) {
  same_as_libc("3.141592653589793238462643383279");
  same_as_libc("0.000000000000001");
  same_as_libc("1.0000000000000000000000000000000000001");
}

void test_very_long_numbers(void) {
  same_as_libc("123456789012345678901234567890");
  same_as_libc("-99999999999999999999999.5");
  // Длинная строка не должна дорасти до inf/nan.
  char buf[400];
  for (int i = 0; i < 399; i++) buf[i] = '1';
  buf[0] = '0';
  buf[1] = '.';
  buf[399] = 0;
  same_as_libc(buf);
}

void test_garbage_yields_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("abc"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("."));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("+"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("- 5"));
}

void test_stops_at_second_dot(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.2f, parseDecimal("1.2.3"));
}

// Что не поддерживается, зафиксировано явно: поведение отличается от libc
// сознательно, портал таких записей не присылает.
void test_unsupported_forms_are_pinned(void) {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, parseDecimal("1e3"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("0x10"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("inf"));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("nan"));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, parseDecimal("5,5"));
  // Дальше 18 знаков после точки цифры отбрасываются: 1e-29 даёт 0.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parseDecimal("0.00000000000000000000000000001"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_libc_on_portal_inputs);
  RUN_TEST(test_negative_zero_is_zero_valued);
  RUN_TEST(test_many_decimals);
  RUN_TEST(test_very_long_numbers);
  RUN_TEST(test_garbage_yields_zero);
  RUN_TEST(test_stops_at_second_dot);
  RUN_TEST(test_unsupported_forms_are_pinned);
  return UNITY_END();
}
