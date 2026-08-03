#include <unity.h>
#include <wifi_state.h>
#include <stdio.h>

static WifiStateMachine sm;

static WifiInputs in(bool creds = true, bool connected = false, bool busy = false,
                     bool apClients = false) {
  WifiInputs i;
  i.hasCredentials = creds;
  i.staConnected = connected;
  i.portalBusy = busy;
  i.apHasClients = apClients;
  return i;
}

void setUp(void) { sm = WifiStateMachine(); }
void tearDown(void) {}

// --- Старт ----------------------------------------------------------------

// Точка доступа на каждой загрузке не светится: сначала тихая попытка.
void test_starts_with_attempt_when_credentials_present(void) {
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.begin(0, in(true)));
  TEST_ASSERT_EQUAL(WifiState::Connecting, sm.state());
  TEST_ASSERT_FALSE(sm.apUp());
}

void test_starts_with_ap_when_no_credentials(void) {
  TEST_ASSERT_EQUAL(WifiAction::OpenAp, sm.begin(0, in(false)));
  TEST_ASSERT_EQUAL(WifiState::ApOnly, sm.state());
  TEST_ASSERT_TRUE(sm.apUp());
}

// --- Успешное подключение -------------------------------------------------

void test_connects_within_timeout(void) {
  sm.begin(0, in(true));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(5000, in(true, true)));
  TEST_ASSERT_EQUAL(WifiState::Connected, sm.state());
}

// Точка не поднималась -- закрывать нечего.
void test_no_close_ap_when_ap_was_never_up(void) {
  sm.begin(0, in(true));
  sm.tick(5000, in(true, true));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(6000, in(true, true)));
}

// --- Неудача и повторы ----------------------------------------------------

void test_raises_ap_after_attempt_timeout(void) {
  sm.begin(0, in(true));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(WIFI_ATTEMPT_TIMEOUT_MS - 1, in(true)));
  TEST_ASSERT_EQUAL(WifiAction::OpenAp, sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true)));
  TEST_ASSERT_EQUAL(WifiState::ApOnly, sm.state());
  TEST_ASSERT_TRUE(sm.apUp());
}

void test_retries_after_first_backoff_step(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));  // -> ApOnly
  uint32_t t = WIFI_ATTEMPT_TIMEOUT_MS;

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(t + WIFI_RETRY_STEPS_MS[0] - 1, in(true)));
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.tick(t + WIFI_RETRY_STEPS_MS[0], in(true)));
  TEST_ASSERT_EQUAL(WifiState::Connecting, sm.state());
}

// Повторная попытка идёт при уже поднятой точке: адаптер уйдёт в AP_STA,
// captive portal и подключённые клиенты не пострадают.
void test_ap_stays_up_during_retry(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS + WIFI_RETRY_STEPS_MS[0], in(true));
  TEST_ASSERT_TRUE_MESSAGE(sm.apUp(), "точка доступа не должна опускаться на время повтора");
}

// Неудачный повтор не перезапускает портал -- иначе слетел бы captive portal.
void test_failed_retry_does_not_reopen_ap(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));
  uint32_t t = WIFI_ATTEMPT_TIMEOUT_MS + WIFI_RETRY_STEPS_MS[0];
  sm.tick(t, in(true));

  TEST_ASSERT_EQUAL(WifiAction::StopAttempt, sm.tick(t + WIFI_ATTEMPT_TIMEOUT_MS, in(true)));
  TEST_ASSERT_EQUAL(WifiState::ApOnly, sm.state());
  TEST_ASSERT_TRUE(sm.apUp());
}

void test_backoff_grows_then_holds(void) {
  sm.begin(0, in(true));
  uint32_t t = 0;
  uint32_t expected[] = {WIFI_RETRY_STEPS_MS[0], WIFI_RETRY_STEPS_MS[1],
                         WIFI_RETRY_STEPS_MS[2], WIFI_RETRY_STEPS_MS[2]};

  for (unsigned i = 0; i < 4; i++) {
    t += WIFI_ATTEMPT_TIMEOUT_MS;
    sm.tick(t, in(true));  // попытка провалилась -> ApOnly

    char msg[48];
    snprintf(msg, sizeof(msg), "повтор %u: пауза не совпала", i);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i], sm.retryLeftMs(t), msg);

    t += expected[i];
    TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.tick(t, in(true)));
  }
}

void test_no_retry_without_credentials(void) {
  sm.begin(0, in(false));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(600000, in(false)));
  TEST_ASSERT_EQUAL(WifiState::ApOnly, sm.state());
}

// --- Портал занят ---------------------------------------------------------

// Повтор не должен сбивать сессию того, кто прямо сейчас вводит пароль.
void test_retry_waits_while_portal_busy(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));
  uint32_t t = WIFI_ATTEMPT_TIMEOUT_MS + WIFI_RETRY_STEPS_MS[0];

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(t, in(true, false, true)));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(t + 60000, in(true, false, true)));
  // браузер ушёл -- пробуем
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.tick(t + 61000, in(true, false, false)));
}

// Точку доступа не опускаем, пока к ней кто-то подключён.
//
// Решает именно число клиентов точки, а не portalBusy. По portalBusy выходило
// две беды сразу: портал, открытый через роутер, не давал закрыть точку вовсе,
// а полуторасекундная пауза в опросе страницы опускала её прямо под тем, кто
// в этот момент к ней подключён.
void test_ap_close_waits_while_ap_has_clients(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));
  uint32_t t = WIFI_ATTEMPT_TIMEOUT_MS + WIFI_RETRY_STEPS_MS[0];
  sm.tick(t, in(true));
  sm.tick(t + 1000, in(true, true));  // подключились, AP ещё поднята

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(t + 2000, in(true, true, false, true)));
  TEST_ASSERT_TRUE(sm.apUp());
  TEST_ASSERT_EQUAL(WifiAction::CloseAp, sm.tick(t + 3000, in(true, true, false, false)));
  TEST_ASSERT_FALSE(sm.apUp());
}

// Браузер на портале через роутер закрытию точки больше не мешает: он к точке
// не подключён, выдёргивать у него нечего.
void test_ap_closes_while_portal_busy_over_sta(void) {
  sm.begin(0, in(true));
  sm.tick(WIFI_ATTEMPT_TIMEOUT_MS, in(true));
  uint32_t t = WIFI_ATTEMPT_TIMEOUT_MS + WIFI_RETRY_STEPS_MS[0];
  sm.tick(t, in(true));
  sm.tick(t + 1000, in(true, true));

  TEST_ASSERT_EQUAL(WifiAction::CloseAp, sm.tick(t + 2000, in(true, true, true, false)));
  TEST_ASSERT_FALSE(sm.apUp());
}

// --- Новые креды ----------------------------------------------------------

void test_new_credentials_attempt_immediately(void) {
  sm.begin(0, in(false));  // AP, кредов нет
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.onCredentialsChanged(5000));
  TEST_ASSERT_EQUAL(WifiState::Connecting, sm.state());
}

void test_new_credentials_reset_backoff(void) {
  sm.begin(0, in(true));
  uint32_t t = 0;
  for (unsigned i = 0; i < 3; i++) {  // разогнать паузу до максимума
    t += WIFI_ATTEMPT_TIMEOUT_MS;
    sm.tick(t, in(true));
    t += sm.retryLeftMs(t);
    sm.tick(t, in(true));
  }

  sm.onCredentialsChanged(t);
  t += WIFI_ATTEMPT_TIMEOUT_MS;
  sm.tick(t, in(true));  // снова неудача
  TEST_ASSERT_EQUAL_UINT32(WIFI_RETRY_STEPS_MS[0], sm.retryLeftMs(t));
}

// Неверные новые креды возвращают в точку доступа, а не оставляют висеть.
void test_bad_new_credentials_fall_back_to_ap(void) {
  sm.begin(0, in(true));
  sm.onCredentialsChanged(1000);
  TEST_ASSERT_EQUAL(WifiAction::OpenAp, sm.tick(1000 + WIFI_ATTEMPT_TIMEOUT_MS, in(true)));
}

// --- Потеря связи после подключения ---------------------------------------

// Потеря связи сначала лечится повторной попыткой, и только если она не
// удалась -- точкой доступа.
//
// Раньше отсюда шли сразу в AP, а подъём точки меняет режим радио и роняет
// связь ещё раз. На железе это давало самоподдерживающийся цикл: устройство
// подключалось, тут же отваливалось и было доступно секунды из каждой минуты.
void test_lost_connection_retries_before_ap(void) {
  sm.begin(0, in(true));
  sm.tick(1000, in(true, true));
  TEST_ASSERT_EQUAL(WifiState::Connected, sm.state());

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(2000, in(true, false)));

  uint32_t lost = 2000 + WIFI_LOST_GRACE_MS;
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt, sm.tick(lost, in(true, false)));
  TEST_ASSERT_EQUAL(WifiState::Connecting, sm.state());
  TEST_ASSERT_FALSE_MESSAGE(sm.apUp(), "точка не должна подниматься до неудачной попытки");
}

// Повтор удался -- точка доступа не понадобилась вовсе.
void test_lost_connection_recovers_without_ap(void) {
  sm.begin(0, in(true));
  sm.tick(1000, in(true, true));

  uint32_t lost = 2000 + WIFI_LOST_GRACE_MS;
  sm.tick(2000, in(true, false));
  sm.tick(lost, in(true, false));  // -> Connecting

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(lost + 3000, in(true, true)));
  TEST_ASSERT_EQUAL(WifiState::Connected, sm.state());
  TEST_ASSERT_FALSE(sm.apUp());
}

// А вот если и повтор не прошёл -- поднимаем точку, чтобы устройство можно
// было перенастроить.
void test_lost_connection_falls_back_to_ap(void) {
  sm.begin(0, in(true));
  sm.tick(1000, in(true, true));

  uint32_t lost = 2000 + WIFI_LOST_GRACE_MS;
  sm.tick(2000, in(true, false));
  sm.tick(lost, in(true, false));  // -> Connecting

  TEST_ASSERT_EQUAL(WifiAction::OpenAp,
                    sm.tick(lost + WIFI_ATTEMPT_TIMEOUT_MS, in(true, false)));
  TEST_ASSERT_EQUAL(WifiState::ApOnly, sm.state());
  TEST_ASSERT_TRUE(sm.apUp());
}

// Короткое моргание связи не должно вообще ничего запускать.
void test_short_blip_does_not_trigger_anything(void) {
  sm.begin(0, in(true));
  sm.tick(1000, in(true, true));

  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(2000, in(true, false)));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(5000, in(true, true)));
  TEST_ASSERT_EQUAL(WifiState::Connected, sm.state());

  // счётчик простоя обнулился: следующее моргание отсчитывается заново
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(6000, in(true, false)));
  TEST_ASSERT_EQUAL(WifiAction::None, sm.tick(6000 + WIFI_LOST_GRACE_MS - 1, in(true, false)));
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt,
                    sm.tick(6000 + WIFI_LOST_GRACE_MS, in(true, false)));
}

// --- Переполнение millis --------------------------------------------------

// millis переполняется примерно через 49 суток, и устройство обязано это
// пережить: разности беззнаковых считаются верно, если не сравнивать напрямую.
void test_survives_millis_overflow(void) {
  uint32_t near = 0xFFFFFFFF - 5000;
  sm.begin(near, in(true));

  uint32_t after = near + WIFI_ATTEMPT_TIMEOUT_MS;  // перевалило через ноль
  TEST_ASSERT_EQUAL(WifiAction::OpenAp, sm.tick(after, in(true)));
  TEST_ASSERT_EQUAL_UINT32(WIFI_RETRY_STEPS_MS[0], sm.retryLeftMs(after));
  TEST_ASSERT_EQUAL(WifiAction::StartAttempt,
                    sm.tick(after + WIFI_RETRY_STEPS_MS[0], in(true)));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_with_attempt_when_credentials_present);
  RUN_TEST(test_starts_with_ap_when_no_credentials);
  RUN_TEST(test_connects_within_timeout);
  RUN_TEST(test_no_close_ap_when_ap_was_never_up);
  RUN_TEST(test_raises_ap_after_attempt_timeout);
  RUN_TEST(test_retries_after_first_backoff_step);
  RUN_TEST(test_ap_stays_up_during_retry);
  RUN_TEST(test_failed_retry_does_not_reopen_ap);
  RUN_TEST(test_backoff_grows_then_holds);
  RUN_TEST(test_no_retry_without_credentials);
  RUN_TEST(test_retry_waits_while_portal_busy);
  RUN_TEST(test_ap_close_waits_while_ap_has_clients);
  RUN_TEST(test_ap_closes_while_portal_busy_over_sta);
  RUN_TEST(test_new_credentials_attempt_immediately);
  RUN_TEST(test_new_credentials_reset_backoff);
  RUN_TEST(test_bad_new_credentials_fall_back_to_ap);
  RUN_TEST(test_lost_connection_retries_before_ap);
  RUN_TEST(test_lost_connection_recovers_without_ap);
  RUN_TEST(test_lost_connection_falls_back_to_ap);
  RUN_TEST(test_short_blip_does_not_trigger_anything);
  RUN_TEST(test_survives_millis_overflow);
  return UNITY_END();
}
