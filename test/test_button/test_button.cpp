#include <unity.h>
#include <Arduino.h>
#include <button.h>

#define BUTTON_PIN 0

// Кнопка замыкает ножку на землю: нажатие -- LOW, покой -- HIGH.
static void press()   { setPinInput(BUTTON_PIN, LOW); }
static void release() { setPinInput(BUTTON_PIN, HIGH); }

void setUp(void) {
  resetArduinoFake();
  release();
}
void tearDown(void) {}

// Прокрутить loop() на заданное время и вернуть первое непустое событие.
// Шаг 10 мс -- порядок реального периода вызова tick() из loop().
static Button::Event run(Button& button, uint32_t ms) {
  Button::Event seen = Button::Event::None;
  for (uint32_t t = 0; t < ms; t += 10) {
    fakeMillis() += 10;
    Button::Event e = button.Tick();
    if (e != Button::Event::None && seen == Button::Event::None) seen = e;
  }
  return seen;
}

// Кнопка -- это вход. Перевод её ножки в выход означал бы, что прошивка
// коротит нажатую кнопку на землю через собственный драйвер.
void test_begin_configures_input(void) {
  Button button;
  button.begin(BUTTON_PIN);

  TEST_ASSERT_EQUAL_MESSAGE(1, pinModeCalls().size(), "лишние вызовы pinMode");
  TEST_ASSERT_EQUAL_MESSAGE(0, pinWrites().size(), "begin пишет в пин кнопки");
}

void test_click_after_debounce(void) {
  Button button;
  button.begin(BUTTON_PIN);

  press();
  TEST_ASSERT_TRUE(run(button, 200) == Button::Event::None);

  release();
  TEST_ASSERT_TRUE_MESSAGE(run(button, 200) == Button::Event::Click,
                           "короткое нажатие не дало Click");
}

// Дребезг контактов не должен превращаться в несколько нажатий.
void test_bounce_gives_single_click(void) {
  Button button;
  button.begin(BUTTON_PIN);

  // Четыре дребезжащих перехода подряд, все короче DEBOUNCE_MS.
  for (int i = 0; i < 4; i++) {
    press();   run(button, 10);
    release(); run(button, 10);
  }
  press();
  run(button, 200);
  release();

  TEST_ASSERT_TRUE_MESSAGE(run(button, 200) == Button::Event::Click,
                           "нажатие после дребезга потеряно");
  TEST_ASSERT_TRUE_MESSAGE(run(button, 1000) == Button::Event::None,
                           "одно нажатие дало второе событие");
}

// Ровно один Hold за удержание, и он не ждёт отпускания: сброс должен
// случиться в руках, а не после того, как кнопку отпустят.
void test_hold_fires_once_without_release(void) {
  Button button;
  button.begin(BUTTON_PIN);

  press();
  TEST_ASSERT_TRUE_MESSAGE(run(button, Button::HOLD_MS + 100) == Button::Event::Hold,
                           "удержание не дало Hold");
  TEST_ASSERT_TRUE_MESSAGE(run(button, 5000) == Button::Event::None,
                           "Hold повторился при том же нажатии");
}

// После Hold отпускание не должно ещё и переключить реле.
void test_hold_does_not_also_click(void) {
  Button button;
  button.begin(BUTTON_PIN);

  press();
  run(button, Button::HOLD_MS + 100);
  release();

  TEST_ASSERT_TRUE_MESSAGE(run(button, 500) == Button::Event::None,
                           "после Hold пришёл лишний Click");
}

// Кнопка, зажатая при подаче питания, -- это заход в режим прошивки, а не
// команда. Её отпускание не должно щёлкнуть реле.
void test_pressed_at_begin_is_ignored(void) {
  press();

  Button button;
  button.begin(BUTTON_PIN);

  run(button, 1000);
  release();
  TEST_ASSERT_TRUE_MESSAGE(run(button, 500) == Button::Event::None,
                           "нажатая при загрузке кнопка дала Click");

  // Дальше кнопка работает как обычно.
  press();
  run(button, 200);
  release();
  TEST_ASSERT_TRUE_MESSAGE(run(button, 200) == Button::Event::Click,
                           "кнопка не взвелась после первого отпускания");
}

// Так же и удержание при загрузке: держать кнопку, пока плата поднимается,
// -- обычное дело, и сбросом настроек это заканчиваться не должно.
void test_held_at_begin_does_not_reset(void) {
  press();

  Button button;
  button.begin(BUTTON_PIN);

  TEST_ASSERT_TRUE_MESSAGE(run(button, Button::HOLD_MS + 1000) == Button::Event::None,
                           "удержание при загрузке дало Hold");
}

// По heldMs() устройство мигает светодиодом, предупреждая о сбросе.
void test_held_ms_tracks_press(void) {
  Button button;
  button.begin(BUTTON_PIN);

  TEST_ASSERT_EQUAL_MESSAGE(0, button.heldMs(), "отпущенная кнопка удерживается");

  press();
  run(button, 1000);
  TEST_ASSERT_TRUE_MESSAGE(button.heldMs() >= 900, "удержание не считается");

  release();
  run(button, 200);
  TEST_ASSERT_EQUAL_MESSAGE(0, button.heldMs(), "после отпускания счёт не сброшен");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_configures_input);
  RUN_TEST(test_click_after_debounce);
  RUN_TEST(test_bounce_gives_single_click);
  RUN_TEST(test_hold_fires_once_without_release);
  RUN_TEST(test_hold_does_not_also_click);
  RUN_TEST(test_pressed_at_begin_is_ignored);
  RUN_TEST(test_held_at_begin_does_not_reset);
  RUN_TEST(test_held_ms_tracks_press);
  return UNITY_END();
}
