#include <unity.h>
#include <ntp_clock.h>
#include <string.h>

using ntp::Action;
using ntp::NtpClock;

static NtpClock clk;

// Момент, заведомо переживший все паузы: пишется как "прошло много времени".
static const uint32_t LATER = 1000000;

void setUp(void) { clk = NtpClock(); }
void tearDown(void) {}

// Довести часы до синхронизированного состояния: резолв, запрос, ответ.
static void sync(uint32_t at, uint32_t epoch) {
  clk.tick(at, true);
  clk.onResolved(at, true);
  clk.tick(at, true);
  clk.onResponse(at, epoch);
}

// --- Порядок действий -----------------------------------------------------

// Сразу после загрузки адреса нет, поэтому первое действие -- резолв.
void test_first_action_is_resolve(void) {
  TEST_ASSERT_EQUAL(Action::Resolve, clk.tick(0, true));
  TEST_ASSERT_FALSE(clk.isTimeSet());
}

// Пока резолв не ответил, повторно его просить нельзя: иначе каждый проход
// loop() запускал бы новый запрос имени.
void test_no_second_resolve_while_resolving(void) {
  clk.tick(0, true);
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(1, true));
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(500, true));
}

void test_send_follows_successful_resolve(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  TEST_ASSERT_EQUAL(Action::Send, clk.tick(0, true));
}

// Отправив запрос, ждём молча -- в этом весь смысл: loop() не блокируется.
void test_waits_quietly_for_response(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  clk.tick(0, true);

  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(1, true));
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(ntp::RESPONSE_TIMEOUT_MS - 1, true));
  TEST_ASSERT_FALSE(clk.isTimeSet());
}

// --- Неудачи --------------------------------------------------------------

// Главная правка: сервер, не ответивший ни разу, не закрепляется. Прежняя
// версия держала его десять попыток подряд, то есть десять минут блокировок.
void test_silent_server_is_dropped_immediately(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  clk.tick(0, true);

  // Таймаут: попытка признана неудачной.
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(ntp::RESPONSE_TIMEOUT_MS, true));

  // Следующая попытка начинается заново с резолва, а не с того же адреса.
  uint32_t next = ntp::RESPONSE_TIMEOUT_MS + ntp::RETRY_INTERVAL_MS;
  TEST_ASSERT_EQUAL(Action::Resolve, clk.tick(next, true));
}

void test_retry_waits_out_the_pause(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  clk.tick(0, true);
  clk.tick(ntp::RESPONSE_TIMEOUT_MS, true);

  uint32_t almost = ntp::RESPONSE_TIMEOUT_MS + ntp::RETRY_INTERVAL_MS - 1;
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(almost, true));
}

void test_failed_resolve_schedules_retry(void) {
  clk.tick(0, true);
  clk.onResolved(0, false);

  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(1, true));
  TEST_ASSERT_EQUAL(Action::Resolve, clk.tick(ntp::RETRY_INTERVAL_MS, true));
}

// --- Связь ----------------------------------------------------------------

void test_no_action_without_link(void) {
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(0, false));
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(LATER, false));
}

// Пропавшая связь снимает незакрытый запрос: иначе он досидел бы до таймаута
// уже после восстановления сети и сжёг бы лишний цикл повтора впустую.
//
// Запрос повторяется на тот же адрес: сервер ничем себя не опорочил, отвечать
// ему было некуда. Лишний резолв здесь означал бы наказание не за то.
void test_lost_link_retries_same_server(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  clk.tick(0, true);

  clk.tick(10, false);

  TEST_ASSERT_EQUAL(Action::Send, clk.tick(20, true));
}

// --- Приём ответа ---------------------------------------------------------

void test_response_sets_time(void) {
  sync(0, 1000);
  TEST_ASSERT_TRUE(clk.isTimeSet());
  TEST_ASSERT_EQUAL_UINT32(1000, clk.epoch(0));
}

// Пакет, которого не ждали, не должен переводить часы: на порт может прилететь
// что угодно, а доверять этому нельзя.
void test_unsolicited_response_ignored(void) {
  clk.onResponse(0, 1000);
  TEST_ASSERT_FALSE(clk.isTimeSet());
}

// Ответ, опоздавший после таймаута, тоже не в счёт -- запроса уже нет.
void test_late_response_ignored(void) {
  clk.tick(0, true);
  clk.onResolved(0, true);
  clk.tick(0, true);
  clk.tick(ntp::RESPONSE_TIMEOUT_MS, true);

  clk.onResponse(ntp::RESPONSE_TIMEOUT_MS + 1, 1000);
  TEST_ASSERT_FALSE(clk.isTimeSet());
}

// --- Ход часов ------------------------------------------------------------

void test_time_advances_with_millis(void) {
  sync(0, 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, clk.epoch(999));
  TEST_ASSERT_EQUAL_UINT32(1001, clk.epoch(1000));
  TEST_ASSERT_EQUAL_UINT32(1060, clk.epoch(60000));
}

void test_next_sync_is_not_immediate(void) {
  sync(0, 1000);
  TEST_ASSERT_EQUAL(Action::Nothing, clk.tick(ntp::SYNC_INTERVAL_MS - 1, true));
  // Адрес себя оправдал, поэтому повтор идёт на него же, без резолва.
  TEST_ASSERT_EQUAL(Action::Send, clk.tick(ntp::SYNC_INTERVAL_MS, true));
}

// --- Часовой пояс ---------------------------------------------------------

void test_offset_shifts_local_time(void) {
  sync(0, 0);  // 1970-01-01 00:00:00 UTC
  clk.setOffset(3 * 3600);
  TEST_ASSERT_EQUAL_UINT8(3, clk.hours(0));
  TEST_ASSERT_EQUAL_UINT8(0, clk.minutes(0));
}

// Отрицательный пояс через полночь. Ровно на этом случае ловится соблазн
// прибавить смещение беззнаково ко всей эпохе: 2^32 не делится на 86400, и
// результат уехал бы на 23296 секунд -- первая версия показывала здесь 5 часов
// вместо 23.
void test_negative_offset_wraps_backwards(void) {
  sync(0, 3600);  // 01:00:00 UTC
  clk.setOffset(-2 * 3600);
  TEST_ASSERT_EQUAL_UINT8(23, clk.hours(0));
}

void test_offset_change_does_not_lose_sync(void) {
  sync(0, 1000);
  clk.setOffset(5 * 3600);
  TEST_ASSERT_TRUE(clk.isTimeSet());
  TEST_ASSERT_EQUAL_UINT32(1000, clk.epoch(0));
}

// --- Форматирование -------------------------------------------------------

void test_formats_time_with_leading_zeros(void) {
  char buf[9];
  sync(0, 3661);  // 01:01:01
  clk.formatTime(0, buf);
  TEST_ASSERT_EQUAL_STRING("01:01:01", buf);
}

void test_formats_end_of_day(void) {
  char buf[9];
  sync(0, 86399);  // 23:59:59
  clk.formatTime(0, buf);
  TEST_ASSERT_EQUAL_STRING("23:59:59", buf);
}

// --- Переполнение millis --------------------------------------------------

// Каждые ~49.7 суток millis обнуляется. Устройство работает месяцами, и на
// этом месте автомат WiFi уже однажды проверялся -- здесь та же арифметика.
void test_survives_millis_overflow(void) {
  uint32_t before = 0xFFFFFF00;
  sync(before, 1000);

  // Момент после переполнения: часы должны идти вперёд, а не отпрыгнуть.
  uint32_t after = before + 2000;  // переполнение внутри
  TEST_ASSERT_EQUAL_UINT32(1002, clk.epoch(after));

  uint32_t due = before + ntp::SYNC_INTERVAL_MS;
  TEST_ASSERT_EQUAL(Action::Send, clk.tick(due, true));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_first_action_is_resolve);
  RUN_TEST(test_no_second_resolve_while_resolving);
  RUN_TEST(test_send_follows_successful_resolve);
  RUN_TEST(test_waits_quietly_for_response);
  RUN_TEST(test_silent_server_is_dropped_immediately);
  RUN_TEST(test_retry_waits_out_the_pause);
  RUN_TEST(test_failed_resolve_schedules_retry);
  RUN_TEST(test_no_action_without_link);
  RUN_TEST(test_lost_link_retries_same_server);
  RUN_TEST(test_response_sets_time);
  RUN_TEST(test_unsolicited_response_ignored);
  RUN_TEST(test_late_response_ignored);
  RUN_TEST(test_time_advances_with_millis);
  RUN_TEST(test_next_sync_is_not_immediate);
  RUN_TEST(test_offset_shifts_local_time);
  RUN_TEST(test_negative_offset_wraps_backwards);
  RUN_TEST(test_offset_change_does_not_lose_sync);
  RUN_TEST(test_formats_time_with_leading_zeros);
  RUN_TEST(test_formats_end_of_day);
  RUN_TEST(test_survives_millis_overflow);
  return UNITY_END();
}
