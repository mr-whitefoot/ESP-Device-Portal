#include <unity.h>
#include <mqtt_reconnect.h>

static MqttReconnect reconnectState;

void setUp(void) { reconnectState = MqttReconnect(); }
void tearDown(void) {}

void test_does_nothing_without_wifi(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(0, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(100000, false, false)));
}

void test_waits_after_wifi_connects(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(1000, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_INITIAL_CONNECT_DELAY_MS - 1, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_INITIAL_CONNECT_DELAY_MS, true, false)));
}

void test_does_not_repeat_while_attempt_is_in_progress(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(
                        MQTT_INITIAL_CONNECT_DELAY_MS +
                            MQTT_CONNECT_TIMEOUT_MS - 1,
                        true, false)));
}

void test_stuck_attempt_is_cancelled_without_blocking(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Disconnect),
                    static_cast<int>(reconnectState.tick(
                        MQTT_INITIAL_CONNECT_DELAY_MS +
                            MQTT_CONNECT_TIMEOUT_MS,
                        true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        MQTT_INITIAL_CONNECT_DELAY_MS +
                            MQTT_CONNECT_TIMEOUT_MS +
                            MQTT_RECONNECT_DELAY_MS,
                        true, false)));
}

void test_retries_after_async_failure(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);
  reconnectState.onDisconnected(1000, true);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_RECONNECT_DELAY_MS - 1, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_RECONNECT_DELAY_MS, true, false)));
}

void test_retries_when_attempt_cannot_be_started(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);
  reconnectState.onAttemptRejected(1000, true);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_RECONNECT_DELAY_MS, true, false)));
}

void test_connected_state_cancels_retry(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);
  reconnectState.onDisconnected(1000, true);

  reconnectState.tick(1001, true, true);
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(100000, true, true)));
}

void test_wifi_loss_cancels_attempt_and_disconnects_transport(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Disconnect),
                    static_cast<int>(reconnectState.tick(501, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(100000, false, false)));
}

void test_wifi_loss_disconnects_active_mqtt(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, true);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Disconnect),
                    static_cast<int>(reconnectState.tick(1000, false, true)));
}

void test_wifi_return_starts_with_initial_delay_again(void) {
  reconnectState.tick(0, true, false);
  reconnectState.tick(MQTT_INITIAL_CONNECT_DELAY_MS, true, false);
  reconnectState.tick(501, false, false);

  reconnectState.tick(1000, true, false);
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_INITIAL_CONNECT_DELAY_MS - 1, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        1000 + MQTT_INITIAL_CONNECT_DELAY_MS, true, false)));
}

void test_timers_survive_millis_overflow(void) {
  const uint32_t nearOverflow = 0xFFFFFFFFu - 200;
  reconnectState.tick(nearOverflow, true, false);

  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::None),
                    static_cast<int>(reconnectState.tick(
                        nearOverflow + MQTT_INITIAL_CONNECT_DELAY_MS - 1,
                        true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(MqttReconnectAction::Connect),
                    static_cast<int>(reconnectState.tick(
                        nearOverflow + MQTT_INITIAL_CONNECT_DELAY_MS,
                        true, false)));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_does_nothing_without_wifi);
  RUN_TEST(test_waits_after_wifi_connects);
  RUN_TEST(test_does_not_repeat_while_attempt_is_in_progress);
  RUN_TEST(test_stuck_attempt_is_cancelled_without_blocking);
  RUN_TEST(test_retries_after_async_failure);
  RUN_TEST(test_retries_when_attempt_cannot_be_started);
  RUN_TEST(test_connected_state_cancels_retry);
  RUN_TEST(test_wifi_loss_cancels_attempt_and_disconnects_transport);
  RUN_TEST(test_wifi_loss_disconnects_active_mqtt);
  RUN_TEST(test_wifi_return_starts_with_initial_delay_again);
  RUN_TEST(test_timers_survive_millis_overflow);
  return UNITY_END();
}
