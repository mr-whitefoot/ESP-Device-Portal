#include <unity.h>
#include <settings.h>
#include <string.h>

// Тесты контракта слоя настроек. Гоняются на memory-бэкенде, но проверяют
// именно контракт, а не реализацию: любой другой бэкенд обязан их проходить.

namespace k {
constexpr settings::Key name{"dev.name"};
constexpr settings::Key invert{"relay.invert"};
constexpr settings::Key port{"mqtt.port"};
constexpr settings::Key offset{"sensor.offset"};
constexpr settings::Key host{"mqtt.host"};
constexpr settings::Key missing{"never.written"};
}  // namespace k

void setUp(void) { settings::resetForTest(); }
void tearDown(void) {}

static const char* str(const settings::Key& key, size_t bufSize = 64) {
  static char buf[128];
  memset(buf, 'X', sizeof(buf));
  settings::getString(key, buf, bufSize);
  return buf;
}

// Ключи различаются, иначе один параметр молча затирал бы другой.
void test_keys_are_distinct(void) {
  TEST_ASSERT_NOT_EQUAL(k::name.id, k::invert.id);
  TEST_ASSERT_NOT_EQUAL(k::port.id, k::offset.id);
  TEST_ASSERT_NOT_EQUAL(k::host.id, k::missing.id);
}

void test_same_name_gives_same_key(void) {
  constexpr settings::Key a{"mqtt.host"};
  TEST_ASSERT_EQUAL_UINT32(k::host.id, a.id);
}

void test_hash_is_compile_time(void) {
  // Если бы хэш считался в рантайме, здесь была бы ошибка компиляции.
  static_assert(settings::Key{"dev.name"}.id == settings::keyHash("dev.name"),
                "хэш ключа должен вычисляться на этапе компиляции");
  TEST_ASSERT_TRUE(true);
}

void test_roundtrip_all_types(void) {
  settings::setString(k::name, "ESP Relay");
  settings::setBool(k::invert, true);
  settings::setInt(k::port, 1883);
  settings::setFloat(k::offset, -1.5f);

  TEST_ASSERT_EQUAL_STRING("ESP Relay", str(k::name));
  TEST_ASSERT_TRUE(settings::getBool(k::invert));
  TEST_ASSERT_EQUAL_INT32(1883, settings::getInt(k::port));
  TEST_ASSERT_EQUAL_FLOAT(-1.5f, settings::getFloat(k::offset));
}

// Главное свойство слоя: добавили параметр -- остальные пережили обновление.
void test_define_does_not_overwrite_existing(void) {
  settings::setString(k::host, "10.0.1.5");
  settings::setInt(k::port, 8883);

  // как при следующей загрузке прошивки с новым параметром
  settings::defineString(k::host, "");
  settings::defineInt(k::port, 1883);
  settings::defineBool(k::invert, false);

  TEST_ASSERT_EQUAL_STRING("10.0.1.5", str(k::host));
  TEST_ASSERT_EQUAL_INT32(8883, settings::getInt(k::port));
  TEST_ASSERT_FALSE(settings::getBool(k::invert));  // новый получил дефолт
}

void test_define_creates_when_absent(void) {
  TEST_ASSERT_FALSE(settings::exists(k::port));
  settings::defineInt(k::port, 1883);
  TEST_ASSERT_TRUE(settings::exists(k::port));
  TEST_ASSERT_EQUAL_INT32(1883, settings::getInt(k::port));
}

// Чтение несуществующего параметра -- не ошибка, а нулевое значение.
void test_missing_key_reads_as_zero(void) {
  TEST_ASSERT_EQUAL_INT32(0, settings::getInt(k::missing));
  TEST_ASSERT_FALSE(settings::getBool(k::missing));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, settings::getFloat(k::missing));
  TEST_ASSERT_EQUAL_STRING("", str(k::missing));
}

void test_bool_is_stored_as_zero_or_one(void) {
  settings::setBool(k::invert, true);
  TEST_ASSERT_EQUAL_INT32(1, settings::getInt(k::invert));
  settings::setBool(k::invert, false);
  TEST_ASSERT_EQUAL_INT32(0, settings::getInt(k::invert));

  // и обратно: любое ненулевое число читается как true
  settings::setInt(k::invert, 42);
  TEST_ASSERT_TRUE(settings::getBool(k::invert));
}

// getString обязан завершать буфер нулём и усекать, а не оставлять мусор.
void test_get_string_truncates_and_terminates(void) {
  settings::setString(k::name, "very long device name");

  char buf[8];
  memset(buf, 'X', sizeof(buf));
  size_t written = settings::getString(k::name, buf, sizeof(buf));

  TEST_ASSERT_EQUAL(7, written);
  TEST_ASSERT_EQUAL_STRING("very lo", buf);
  TEST_ASSERT_EQUAL('\0', buf[7]);
}

void test_get_string_into_tiny_buffer(void) {
  settings::setString(k::name, "abc");

  char one[1] = {'X'};
  TEST_ASSERT_EQUAL(0, settings::getString(k::name, one, sizeof(one)));
  TEST_ASSERT_EQUAL('\0', one[0]);

  char untouched[1] = {'X'};
  TEST_ASSERT_EQUAL(0, settings::getString(k::name, untouched, 0));
  TEST_ASSERT_EQUAL('X', untouched[0]);

  TEST_ASSERT_EQUAL(0, settings::getString(k::name, nullptr, 8));
}

void test_empty_string_roundtrip(void) {
  settings::setString(k::name, "");
  TEST_ASSERT_TRUE(settings::exists(k::name));
  TEST_ASSERT_EQUAL_STRING("", str(k::name));

  settings::setString(k::name, nullptr);
  TEST_ASSERT_EQUAL_STRING("", str(k::name));
}

void test_overwrite_changes_value(void) {
  settings::setInt(k::port, 1883);
  settings::setInt(k::port, 8883);
  TEST_ASSERT_EQUAL_INT32(8883, settings::getInt(k::port));
}

void test_clear_removes_everything(void) {
  settings::setInt(k::port, 1883);
  settings::setString(k::host, "10.0.1.5");

  settings::clear();

  TEST_ASSERT_FALSE(settings::exists(k::port));
  TEST_ASSERT_FALSE(settings::exists(k::host));
  TEST_ASSERT_EQUAL_INT32(0, settings::getInt(k::port));
}

// Запись на носитель откладывается: иначе каждое переключение реле
// перезаписывало бы файл базы.
void test_writes_are_deferred_until_commit(void) {
  settings::setInt(k::port, 1883);
  settings::setString(k::host, "10.0.1.5");
  TEST_ASSERT_EQUAL(0, settings::detail::commitCount());

  settings::commit();
  TEST_ASSERT_EQUAL(1, settings::detail::commitCount());
}

void test_commit_without_changes_does_nothing(void) {
  settings::setInt(k::port, 1883);
  settings::commit();
  settings::commit();
  settings::tick();
  TEST_ASSERT_EQUAL(1, settings::detail::commitCount());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_keys_are_distinct);
  RUN_TEST(test_same_name_gives_same_key);
  RUN_TEST(test_hash_is_compile_time);
  RUN_TEST(test_roundtrip_all_types);
  RUN_TEST(test_define_does_not_overwrite_existing);
  RUN_TEST(test_define_creates_when_absent);
  RUN_TEST(test_missing_key_reads_as_zero);
  RUN_TEST(test_bool_is_stored_as_zero_or_one);
  RUN_TEST(test_get_string_truncates_and_terminates);
  RUN_TEST(test_get_string_into_tiny_buffer);
  RUN_TEST(test_empty_string_roundtrip);
  RUN_TEST(test_overwrite_changes_value);
  RUN_TEST(test_clear_removes_everything);
  RUN_TEST(test_writes_are_deferred_until_commit);
  RUN_TEST(test_commit_without_changes_does_nothing);
  return UNITY_END();
}
