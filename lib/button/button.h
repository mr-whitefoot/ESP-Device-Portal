#pragma once
#include <Arduino.h>

// Физическая кнопка на корпусе платы.
//
// Здесь только опрос ножки: антидребезг и разделение короткого нажатия и
// удержания. Что делать по событию, решает устройство -- класс не знает ни
// про реле, ни про настройки, поэтому и покрывается native-тестами целиком.
//
// Не путать с настройкой "Button mode" у реле: та про импульс на выходе,
// эта -- про вход.
class Button {
  public:
    // Дребезг механической кнопки укладывается в единицы миллисекунд.
    // 50 мс -- запас, при котором ни одно настоящее нажатие ещё не теряется.
    static constexpr uint32_t DEBOUNCE_MS = 50;

    // Удержание до срабатывания Hold. Десять секунд заметно дольше, чем
    // кнопку можно придержать случайно, а по этому событию устройство делает
    // необратимое: сброс к заводским настройкам.
    static constexpr uint32_t HOLD_MS = 10000;

    enum class Event : uint8_t {
      None,
      Click,  // отпущена раньше HOLD_MS
      Hold,   // удерживается HOLD_MS; отдаётся один раз, не дожидаясь отпускания
    };

    // Конструктор не трогает железо по той же причине, что и у ESPRelay:
    // объект глобальный и создаётся до setup().
    Button() {}

    // activeLow -- кнопка замыкает ножку на землю, то есть нажатие это LOW.
    void begin( int pin, bool activeLow = true ){
      this->pin = pin;
      this->activeLow = activeLow;

      // INPUT, а не INPUT_PULLUP: на таких платах кнопка подтянута внешним
      // резистором, а GPIO0 к тому же загрузочный -- включать на нём ещё и
      // внутреннюю подтяжку незачем.
      pinMode(pin, INPUT);

      stablePressed = rawPressed();
      lastRaw = stablePressed;
      lastRawAt = millis();
      pressedAt = lastRawAt;
      holdFired = false;

      // Кнопка, уже нажатая в момент begin(), событий не даёт: так выглядит
      // удержание при подаче питания. Ждём первого отпускания -- оно и
      // взводит кнопку.
      armed = !stablePressed;
    }

    // Зовётся из loop(). Одно событие за вызов: длиннее очереди у кнопки
    // не бывает -- нажатие и отпускание разделены дребезгом.
    Event Tick(){
      uint32_t now = millis();
      bool raw = rawPressed();

      // Уровень дёрнулся -- отсчёт устойчивости начинается заново.
      if (raw != lastRaw){
        lastRaw = raw;
        lastRawAt = now;
        return Event::None;
      }

      // Устоялся и отличается от принятого положения -- принимаем новое.
      if (raw != stablePressed && (uint32_t)(now - lastRawAt) >= DEBOUNCE_MS){
        stablePressed = raw;

        if (raw){
          pressedAt = now;
          holdFired = false;
          return Event::None;
        }

        // Отпускание. Клик не отдаём дважды за одно нажатие: если Hold уже
        // сработал, отпускание -- это просто конец удержания.
        bool click = armed && !holdFired;
        armed = true;
        return click ? Event::Click : Event::None;
      }

      if (stablePressed && armed && !holdFired &&
          (uint32_t)(now - pressedAt) >= HOLD_MS){
        holdFired = true;
        return Event::Hold;
      }

      return Event::None;
    }

    // Сколько кнопка удерживается прямо сейчас; 0, если отпущена или ещё не
    // взведена. Нужно устройству для обратной связи: удерживать десять секунд
    // вслепую, без признака того, что нажатие вообще замечено, невозможно.
    uint32_t heldMs() const {
      if (!stablePressed || !armed) return 0;
      return (uint32_t)(millis() - pressedAt);
    }

  private:
    bool rawPressed() const {
      return digitalRead(pin) == (activeLow ? LOW : HIGH);
    }

    int pin = 0;
    bool activeLow = true;
    // Положение, принятое за настоящее: пережившее DEBOUNCE_MS.
    bool stablePressed = false;
    // Последний прочитанный уровень и момент, когда он появился.
    bool lastRaw = false;
    uint32_t lastRawAt = 0;
    uint32_t pressedAt = 0;
    bool holdFired = false;
    bool armed = false;
};
