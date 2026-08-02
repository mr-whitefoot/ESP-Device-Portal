#include <unity.h>
#include <timezone_table.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Разбор подписи "+05:45" / "-09:30" / "00:00" в минуты.
// Тест считает смещение из того, что видит пользователь в выпадающем списке,
// и сверяет с тем, что уйдёт в NTP-клиент.
static int parseLabelMinutes(const char* label) {
  int sign = 1;
  const char* p = label;
  if (*p == '-') { sign = -1; p++; }
  else if (*p == '+') { p++; }

  const char* colon = strchr(p, ':');
  TEST_ASSERT_NOT_NULL_MESSAGE(colon, "в подписи нет двоеточия");

  int hours = atoi(p);
  int minutes = atoi(colon + 1);
  return sign * (hours * 60 + minutes);
}

void test_label_matches_offset(void) {
  for (uint8_t i = 0; i < TIMEZONE_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "индекс %u, подпись %s", i, TIMEZONES[i].label);
    TEST_ASSERT_EQUAL_INT_MESSAGE(parseLabelMinutes(TIMEZONES[i].label),
                                  TIMEZONES[i].offsetMinutes, msg);
  }
}

void test_offsets_strictly_increase(void) {
  for (uint8_t i = 1; i < TIMEZONE_COUNT; i++) {
    char msg[64];
    snprintf(msg, sizeof(msg), "порядок нарушен на индексе %u", i);
    TEST_ASSERT_TRUE_MESSAGE(TIMEZONES[i].offsetMinutes > TIMEZONES[i - 1].offsetMinutes, msg);
  }
}

// Дубликат `timezone == 19` в старой цепочке if-ов делал +04:00 недостижимым.
void test_no_duplicate_offsets(void) {
  for (uint8_t i = 0; i < TIMEZONE_COUNT; i++)
    for (uint8_t j = i + 1; j < TIMEZONE_COUNT; j++)
      TEST_ASSERT_NOT_EQUAL(TIMEZONES[i].offsetMinutes, TIMEZONES[j].offsetMinutes);
}

void test_full_range_covered(void) {
  TEST_ASSERT_EQUAL_UINT8(37, TIMEZONE_COUNT);
  TEST_ASSERT_EQUAL_INT(-12 * 3600, tzOffsetSeconds(0));
  TEST_ASSERT_EQUAL_INT(14 * 3600, tzOffsetSeconds(TIMEZONE_COUNT - 1));
}

// Старая функция была 1-based, а GP.SELECT отдаёт 0-based: выбор "00:00"
// давал -01:00. Здесь индекс по умолчанию обязан означать ровно UTC.
void test_default_index_is_utc(void) {
  TEST_ASSERT_EQUAL_INT(0, tzOffsetSeconds(TIMEZONE_UTC));
  TEST_ASSERT_EQUAL_STRING("00:00", TIMEZONES[TIMEZONE_UTC].label);
}

void test_out_of_range_falls_back_to_utc(void) {
  TEST_ASSERT_EQUAL_INT(0, tzOffsetSeconds(TIMEZONE_COUNT));
  TEST_ASSERT_EQUAL_INT(0, tzOffsetSeconds(255));
}

// Пояса с некратным часу смещением легко потерять при ручном пересчёте.
void test_half_and_quarter_hour_zones(void) {
  TEST_ASSERT_EQUAL_INT(345 * 60, tzOffsetSeconds(23));   // +05:45
  TEST_ASSERT_EQUAL_INT(525 * 60, tzOffsetSeconds(28));   // +08:45
  TEST_ASSERT_EQUAL_INT(-210 * 60, tzOffsetSeconds(10));  // -03:30
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_label_matches_offset);
  RUN_TEST(test_offsets_strictly_increase);
  RUN_TEST(test_no_duplicate_offsets);
  RUN_TEST(test_full_range_covered);
  RUN_TEST(test_default_index_is_utc);
  RUN_TEST(test_out_of_range_falls_back_to_utc);
  RUN_TEST(test_half_and_quarter_hour_zones);
  return UNITY_END();
}
