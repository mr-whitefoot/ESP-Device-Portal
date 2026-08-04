#include <unity.h>
#include <restart_request.h>

static RestartRequest rr;

void setUp(void) { rr = RestartRequest(); }
void tearDown(void) {}

// Никто не просил -- перезагрузки нет, сколько бы времени ни прошло.
void test_idle_never_fires(void) {
  TEST_ASSERT_FALSE(rr.pending());
  TEST_ASSERT_FALSE(rr.tick(0));
  TEST_ASSERT_FALSE(rr.tick(1000000));
}

// Смысл всей правки: сразу после просьбы перезагружаться нельзя, иначе
// ответ на форму не успевает уйти.
void test_does_not_fire_immediately(void) {
  rr.request(1000);
  TEST_ASSERT_TRUE(rr.pending());
  TEST_ASSERT_FALSE(rr.tick(1000));
  TEST_ASSERT_FALSE(rr.tick(1000 + RESTART_DELAY_MS - 1));
}

void test_fires_after_delay(void) {
  rr.request(1000);
  TEST_ASSERT_TRUE(rr.tick(1000 + RESTART_DELAY_MS));
}

// На железе после true управление не возвращается, но полагаться на это
// в логике нельзя: срабатывание одно.
void test_fires_once(void) {
  rr.request(0);
  TEST_ASSERT_TRUE(rr.tick(RESTART_DELAY_MS));
  TEST_ASSERT_FALSE(rr.tick(RESTART_DELAY_MS * 10));
  TEST_ASSERT_FALSE(rr.pending());
}

// Повторная просьба не отодвигает срок. Иначе браузер, дёргающий страницу
// раз в секунду, откладывал бы перезагрузку сколько угодно долго.
void test_repeated_request_does_not_postpone(void) {
  rr.request(0);
  rr.request(RESTART_DELAY_MS - 1);
  TEST_ASSERT_TRUE(rr.tick(RESTART_DELAY_MS));
}

// После срабатывания механизм снова готов: перезагрузка могла и не случиться
// (в тестах её нет вовсе), а следующая просьба обязана отсчитываться заново.
void test_can_be_requested_again(void) {
  rr.request(0);
  rr.tick(RESTART_DELAY_MS);

  rr.request(5000);
  TEST_ASSERT_FALSE(rr.tick(5000 + RESTART_DELAY_MS - 1));
  TEST_ASSERT_TRUE(rr.tick(5000 + RESTART_DELAY_MS));
}

// Признак нужен, чтобы в этом окне не публиковать в MQTT: после factory reset
// периодическое сообщение вернуло бы брокеру только что снятый топик.
void test_pending_is_visible_until_it_fires(void) {
  TEST_ASSERT_FALSE(rr.pending());
  rr.request(0);
  TEST_ASSERT_TRUE(rr.pending());
  rr.tick(RESTART_DELAY_MS - 1);
  TEST_ASSERT_TRUE(rr.pending());
  rr.tick(RESTART_DELAY_MS);
  TEST_ASSERT_FALSE(rr.pending());
}

// millis переполняется примерно раз в 49 суток, и просьба может прийти прямо
// перед этим. Разность беззнаковых считается верно, если не сравнивать
// величины напрямую.
void test_survives_millis_overflow(void) {
  uint32_t near = 0xFFFFFFFF - (RESTART_DELAY_MS / 2);
  rr.request(near);

  uint32_t after = near + RESTART_DELAY_MS;  // перевалило через ноль
  TEST_ASSERT_FALSE(rr.tick(after - 1));
  TEST_ASSERT_TRUE(rr.tick(after));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_never_fires);
  RUN_TEST(test_does_not_fire_immediately);
  RUN_TEST(test_fires_after_delay);
  RUN_TEST(test_fires_once);
  RUN_TEST(test_repeated_request_does_not_postpone);
  RUN_TEST(test_can_be_requested_again);
  RUN_TEST(test_pending_is_visible_until_it_fires);
  RUN_TEST(test_survives_millis_overflow);
  return UNITY_END();
}
