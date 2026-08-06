#include <unity.h>
#include <relay_bank_schedule.h>
#include <relay_bank_keys.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_all_eight_targets_are_preserved(void) {
  for (int32_t i = 0; i < RELAY_BANK_COUNT; i++)
    TEST_ASSERT_EQUAL_UINT8(i, relayBankTarget(i));
}

void test_all_relays_target_is_preserved(void) {
  TEST_ASSERT_EQUAL_UINT8(RELAY_BANK_ALL_TARGET,
                          relayBankTarget(RELAY_BANK_ALL_TARGET));
}

void test_invalid_targets_fall_back_to_first_relay(void) {
  TEST_ASSERT_EQUAL_UINT8(0, relayBankTarget(-1));
  TEST_ASSERT_EQUAL_UINT8(0, relayBankTarget(RELAY_BANK_ALL_TARGET + 1));
  TEST_ASSERT_EQUAL_UINT8(0, relayBankTarget(1000));
}

void test_all_relays_target_includes_every_channel(void) {
  for (uint8_t relay = 0; relay < RELAY_BANK_COUNT; relay++)
    TEST_ASSERT_TRUE(relayBankTargetIncludes(RELAY_BANK_ALL_TARGET, relay));
  TEST_ASSERT_FALSE(
      relayBankTargetIncludes(RELAY_BANK_ALL_TARGET, RELAY_BANK_COUNT));
}

void test_single_target_includes_only_selected_channel(void) {
  for (uint8_t target = 0; target < RELAY_BANK_COUNT; target++) {
    for (uint8_t relay = 0; relay < RELAY_BANK_COUNT; relay++)
      TEST_ASSERT_EQUAL(target == relay,
                        relayBankTargetIncludes(target, relay));
  }
}

void test_entity_ids_are_stable_and_distinct(void) {
  const char* expected[RELAY_BANK_COUNT] = {
      "relay_1", "relay_2", "relay_3", "relay_4",
      "relay_5", "relay_6", "relay_7", "relay_8",
  };

  for (uint8_t i = 0; i < RELAY_BANK_COUNT; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], RELAY_BANK_ENTITY_IDS[i]);
    for (uint8_t j = i + 1; j < RELAY_BANK_COUNT; j++)
      TEST_ASSERT_NOT_EQUAL(0, strcmp(RELAY_BANK_ENTITY_IDS[i],
                                      RELAY_BANK_ENTITY_IDS[j]));
  }
}

void test_bank_setting_keys_do_not_collide(void) {
  struct Group {
    const settings::Key* keys;
    size_t count;
  };
  const Group groups[] = {
      {keys::relayBank::label, RELAY_BANK_COUNT},
      {keys::relayBank::invert, RELAY_BANK_COUNT},
      {keys::relayBank::buttonMode, RELAY_BANK_COUNT},
      {keys::relayBank::saveState, RELAY_BANK_COUNT},
      {keys::relayBank::state, RELAY_BANK_COUNT},
      {keys::bankTimer::enable, TIMER_COUNT},
      {keys::bankTimer::action, TIMER_COUNT},
      {keys::bankTimer::hours, TIMER_COUNT},
      {keys::bankTimer::minutes, TIMER_COUNT},
      {keys::bankTimer::seconds, TIMER_COUNT},
      {keys::bankTimer::target, TIMER_COUNT},
  };

  for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
    for (size_t i = 0; i < groups[g].count; i++) {
      for (size_t h = g; h < sizeof(groups) / sizeof(groups[0]); h++) {
        size_t start = h == g ? i + 1 : 0;
        for (size_t j = start; j < groups[h].count; j++)
          TEST_ASSERT_NOT_EQUAL(groups[g].keys[i].id, groups[h].keys[j].id);
      }
    }
  }
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_all_eight_targets_are_preserved);
  RUN_TEST(test_all_relays_target_is_preserved);
  RUN_TEST(test_invalid_targets_fall_back_to_first_relay);
  RUN_TEST(test_all_relays_target_includes_every_channel);
  RUN_TEST(test_single_target_includes_only_selected_channel);
  RUN_TEST(test_entity_ids_are_stable_and_distinct);
  RUN_TEST(test_bank_setting_keys_do_not_collide);
  return UNITY_END();
}
