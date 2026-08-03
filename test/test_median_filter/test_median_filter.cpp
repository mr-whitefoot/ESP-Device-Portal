#include <math.h>
#include <median_filter.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// Диапазон DS18B20 по даташиту. Ошибки библиотеки (-127 обрыв, -128 CRC)
// лежат за его пределами и должны отбрасываться самим диапазоном.
static const float MIN_C = -55.0f;
static const float MAX_C = 125.0f;

// Пока ни одного достоверного значения, спрашивать нечего. Ноль здесь
// означал бы "0 °C", а не "не знаю", поэтому решает ready(), а не value().
void test_empty_filter_is_not_ready(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  TEST_ASSERT_FALSE(f.ready());
  TEST_ASSERT_EQUAL_UINT8(0, f.count());
}

// Медиана по набранному, а не по всему окну: результат осмыслен с первого
// чтения, а не через девять периодов опроса.
void test_single_value_is_the_median(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  TEST_ASSERT_TRUE(f.add(21.5f));
  TEST_ASSERT_TRUE(f.ready());
  TEST_ASSERT_EQUAL_FLOAT(21.5f, f.value());
}

void test_median_of_odd_count(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  f.add(5.0f);
  f.add(1.0f);
  f.add(3.0f);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, f.value());
}

void test_median_of_even_count_averages_two_middle(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  f.add(1.0f);
  f.add(2.0f);
  f.add(3.0f);
  f.add(4.0f);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, f.value());
}

// Порядок поступления не влияет на результат.
void test_order_does_not_matter(void) {
  MedianFilter<5> a(MIN_C, MAX_C), b(MIN_C, MAX_C);
  const float asc[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  const float desc[] = {5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
  for (int i = 0; i < 5; i++) {
    a.add(asc[i]);
    b.add(desc[i]);
  }
  TEST_ASSERT_EQUAL_FLOAT(a.value(), b.value());
}

// Одиночный выброс не сдвигает медиану -- ради этого фильтр и нужен.
void test_single_spike_does_not_move_median(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  for (int i = 0; i < 8; i++) f.add(20.0f);
  f.add(120.0f);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, f.value());
}

// Окно скользящее: старые значения вытесняются по возрасту. Прежняя
// реализация возраст не учитывала вовсе -- вытеснялся край набора, поэтому
// значение могло пережить сколько угодно новых чтений.
void test_window_forgets_oldest_values(void) {
  MedianFilter<3> f(MIN_C, MAX_C);
  f.add(1.0f);
  f.add(2.0f);
  f.add(3.0f);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, f.value());

  f.add(10.0f);
  f.add(10.0f);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, f.value());
  TEST_ASSERT_EQUAL_UINT8(3, f.count());
}

void test_count_stops_at_window_size(void) {
  MedianFilter<3> f(MIN_C, MAX_C);
  for (int i = 0; i < 10; i++) f.add(20.0f);
  TEST_ASSERT_EQUAL_UINT8(3, f.count());
}

// Главное отличие от прежней реализации: коды ошибок библиотеки DS18B20
// приходят обычными числами и раньше попадали в выборку наравне с
// температурой. Отключённый датчик через пять чтений выдавал в
// HomeAssistant честные -127 °C.
void test_sensor_error_values_are_rejected(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  f.add(20.0f);

  TEST_ASSERT_FALSE(f.add(-127.0f));  // DEVICE_DISCONNECTED
  TEST_ASSERT_FALSE(f.add(-128.0f));  // DEVICE_CRC_ERROR

  // Окно не тронуто: отброшенное значение не занимает места и не сдвигает
  // возраст остальных.
  TEST_ASSERT_EQUAL_UINT8(1, f.count());
  TEST_ASSERT_EQUAL_FLOAT(20.0f, f.value());
}

void test_out_of_range_and_nan_are_rejected(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  TEST_ASSERT_FALSE(f.add(NAN));
  TEST_ASSERT_FALSE(f.add(125.5f));
  TEST_ASSERT_FALSE(f.add(-55.1f));
  TEST_ASSERT_FALSE(f.ready());
}

// Границы диапазона -- достоверные показания, а не ошибки.
void test_range_bounds_are_accepted(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  TEST_ASSERT_TRUE(f.add(MIN_C));
  TEST_ASSERT_TRUE(f.add(MAX_C));
  TEST_ASSERT_EQUAL_UINT8(2, f.count());
}

// Отрицательная температура с холодного старта. Прежняя реализация
// начинала с массива нулей, и они оставались частью выборки: уличный
// датчик первые чтения показывал 0 °C вместо мороза.
void test_cold_start_below_zero(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  f.add(-12.0f);
  TEST_ASSERT_EQUAL_FLOAT(-12.0f, f.value());
  f.add(-12.0f);
  f.add(-13.0f);
  TEST_ASSERT_EQUAL_FLOAT(-12.0f, f.value());
}

// Ноль -- обычное показание, а не признак пустой ячейки. Прежний код
// отличал их сравнением с NULL и на нуле градусов срывался на нефильтрованное
// значение.
void test_zero_is_a_valid_reading(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  TEST_ASSERT_TRUE(f.add(0.0f));
  TEST_ASSERT_TRUE(f.ready());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, f.value());
}

void test_reset_clears_the_window(void) {
  MedianFilter<9> f(MIN_C, MAX_C);
  f.add(20.0f);
  f.add(21.0f);
  f.reset();
  TEST_ASSERT_FALSE(f.ready());
  TEST_ASSERT_EQUAL_UINT8(0, f.count());

  f.add(30.0f);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, f.value());
}

// Окно из одного значения вырождается в отсутствие фильтрации, но остаётся
// рабочим: край диапазона размеров не должен требовать особого случая.
void test_window_of_one(void) {
  MedianFilter<1> f(MIN_C, MAX_C);
  f.add(20.0f);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, f.value());
  f.add(30.0f);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, f.value());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_filter_is_not_ready);
  RUN_TEST(test_single_value_is_the_median);
  RUN_TEST(test_median_of_odd_count);
  RUN_TEST(test_median_of_even_count_averages_two_middle);
  RUN_TEST(test_order_does_not_matter);
  RUN_TEST(test_single_spike_does_not_move_median);
  RUN_TEST(test_window_forgets_oldest_values);
  RUN_TEST(test_count_stops_at_window_size);
  RUN_TEST(test_sensor_error_values_are_rejected);
  RUN_TEST(test_out_of_range_and_nan_are_rejected);
  RUN_TEST(test_range_bounds_are_accepted);
  RUN_TEST(test_cold_start_below_zero);
  RUN_TEST(test_zero_is_a_valid_reading);
  RUN_TEST(test_reset_clears_the_window);
  RUN_TEST(test_window_of_one);
  return UNITY_END();
}
