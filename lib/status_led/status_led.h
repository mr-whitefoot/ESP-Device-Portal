#pragma once
#include <Arduino.h>

// Светодиод состояния на плате.
//
// Отдельно от ESPRelay намеренно: у реле есть импульсный режим, колбэк смены
// состояния и сохранение во флеш, и ничего из этого светодиоду не нужно.
// Общая у них ровно одна вещь -- порядок инициализации, и она здесь главная.
class StatusLed {
  public:
    // Конструктор не трогает железо: объект глобальный, создаётся до setup().
    StatusLed() {}

    // activeLow -- светодиод зажигается низким уровнем (катод на ножке).
    // on -- положение, с которым он должен подняться.
    void begin( int pin, bool activeLow = true, bool on = false ){
      this->pin = pin;
      this->activeLow = activeLow;
      this->state = on;

      // Уровень выставляется ДО pinMode(OUTPUT) -- по той же причине, что и
      // в ESPRelay::begin(): защёлка выхода после сброса нулевая, а этот
      // светодиод горит именно от LOW. Перевод пина в выход без
      // предварительной записи зажёг бы его на всю загрузку.
      digitalWrite(pin, levelFor(on));
      pinMode(pin, OUTPUT);
    }

    // Сравнения с прежним состоянием нет намеренно: Set() зовут в том числе
    // из мигания по таймеру, и лишняя ветка тут дороже самой записи в пин.
    void Set( bool on ){
      state = on;
      digitalWrite(pin, levelFor(on));
    }

    bool Get() const { return state; }

  private:
    int levelFor( bool on ) const { return (on != activeLow) ? HIGH : LOW; }

    int pin = 0;
    bool activeLow = true;
    bool state = false;
};
