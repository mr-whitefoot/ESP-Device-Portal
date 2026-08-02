#include <unity.h>
#include <timer_schedule.h>
#include <string.h>

static Timers timers;
static TimerScheduler scheduler;

void setUp(void) {
  memset(&timers, 0, sizeof(timers));
  scheduler = TimerScheduler();
}
void tearDown(void) {}

static void setTimer(uint8_t i, uint8_t h, uint8_t m, uint8_t s, uint8_t action = TIMER_ACTION_ON) {
  timers.timer[i].enable = true;
  timers.timer[i].action = action;
  timers.timer[i].hours = h;
  timers.timer[i].minutes = m;
  timers.timer[i].seconds = s;
}

static uint32_t at(uint8_t h, uint8_t m, uint8_t s) {
  return (uint32_t)h * 3600 + (uint32_t)m * 60 + s;
}

// Первый вызов только запоминает точку отсчёта, иначе после включения
// сработали бы все таймеры, чьё время уже прошло сегодня.
void test_first_call_never_fires(void) {
  setTimer(0, 12, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 0)));
}

void test_fires_exactly_on_time(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(12, 0, 0)));
}

// Ядро бага: тик loop() задержался, секунда 12:00:00 не наблюдалась ни разу.
void test_fires_when_second_was_skipped(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 58));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(12, 0, 2)));
}

void test_fires_after_long_but_allowed_gap(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 0));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(12, 1, 0)));
}

// Второй бок той же медали: два вызова внутри одной секунды давали
// двойное Toggle, то есть возврат в исходное состояние.
void test_does_not_fire_twice_in_same_second(void) {
  setTimer(0, 12, 0, 0, TIMER_ACTION_TOGGLE);
  scheduler.due(timers, at(11, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(12, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 0)));
}

void test_does_not_fire_twice_on_next_second(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(12, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 1)));
}

void test_disabled_timer_never_fires(void) {
  setTimer(0, 12, 0, 0);
  timers.timer[0].enable = false;
  scheduler.due(timers, at(11, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 0)));
}

void test_multiple_timers_in_one_interval(void) {
  setTimer(0, 12, 0, 1);
  setTimer(2, 12, 0, 2);
  setTimer(4, 12, 0, 3);
  scheduler.due(timers, at(12, 0, 0));

  uint32_t mask = scheduler.due(timers, at(12, 0, 4));
  TEST_ASSERT_EQUAL_UINT32((1 << 0) | (1 << 2) | (1 << 4), mask);
}

void test_midnight_wrap(void) {
  setTimer(0, 0, 0, 0);
  scheduler.due(timers, at(23, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(0, 0, 0)));
}

void test_midnight_wrap_with_skipped_second(void) {
  setTimer(0, 0, 0, 0);
  scheduler.due(timers, at(23, 59, 58));
  TEST_ASSERT_EQUAL_UINT32(1, scheduler.due(timers, at(0, 0, 2)));
}

// После долгого обрыва связи отрабатывать разом всё пропущенное неправильно.
void test_long_outage_does_not_replay_missed_timers(void) {
  setTimer(0, 12, 0, 0);
  setTimer(1, 13, 0, 0);
  scheduler.due(timers, at(11, 0, 0));

  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(14, 0, 0)));
  // и после паузы расписание продолжает работать
  setTimer(2, 14, 0, 30);
  TEST_ASSERT_EQUAL_UINT32(1 << 2, scheduler.due(timers, at(14, 0, 30)));
}

// Коррекция часов назад по значению неотличима от полуночи.
void test_backward_clock_jump_does_not_fire(void) {
  setTimer(0, 5, 0, 0);
  scheduler.due(timers, at(12, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(10, 0, 0)));
}

void test_resync_skips_one_interval(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 58));
  scheduler.resync();
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, at(12, 0, 0)));
  // а дальше работает как обычно
  setTimer(1, 12, 0, 5);
  TEST_ASSERT_EQUAL_UINT32(1 << 1, scheduler.due(timers, at(12, 0, 5)));
}

void test_garbage_time_is_ignored(void) {
  setTimer(0, 12, 0, 0);
  scheduler.due(timers, at(11, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, SECONDS_PER_DAY));
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.due(timers, 999999));
}

void test_second_of_day_conversion(void) {
  Timer t = {true, TIMER_ACTION_ON, 23, 59, 59};
  TEST_ASSERT_EQUAL_UINT32(86399, timerSecondOfDay(t));

  Timer midnight = {true, TIMER_ACTION_ON, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(0, timerSecondOfDay(midnight));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_first_call_never_fires);
  RUN_TEST(test_fires_exactly_on_time);
  RUN_TEST(test_fires_when_second_was_skipped);
  RUN_TEST(test_fires_after_long_but_allowed_gap);
  RUN_TEST(test_does_not_fire_twice_in_same_second);
  RUN_TEST(test_does_not_fire_twice_on_next_second);
  RUN_TEST(test_disabled_timer_never_fires);
  RUN_TEST(test_multiple_timers_in_one_interval);
  RUN_TEST(test_midnight_wrap);
  RUN_TEST(test_midnight_wrap_with_skipped_second);
  RUN_TEST(test_long_outage_does_not_replay_missed_timers);
  RUN_TEST(test_backward_clock_jump_does_not_fire);
  RUN_TEST(test_resync_skips_one_interval);
  RUN_TEST(test_garbage_time_is_ignored);
  RUN_TEST(test_second_of_day_conversion);
  return UNITY_END();
}
