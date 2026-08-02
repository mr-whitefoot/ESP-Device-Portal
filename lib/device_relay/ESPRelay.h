#include <Arduino.h>
class ESPRelay{
  public:
    // Конструктор намеренно не трогает железо: объект глобальный, а значит
    // создаётся до setup(). Раньше здесь были pinMode() и SetState(false),
    // и пин уходил в LOW ещё до чтения настроек -- на плате с активным низким
    // уровнем реле было включено всю загрузку, до вызова SetInvertMode().
    ESPRelay(int pin = 0, bool invertMode = false )
      : pin(pin), invertMode(invertMode) {}

    void ChangeStateCallback( void (*handler)()) {
        CallbackHandler = handler;
    }

    // Инверсия обязана быть известна до первой записи в пин, поэтому пин
    // и режим настраиваются одним вызовом.
    //
    // initialState -- состояние, с которым реле должно подняться. Передавать
    // сюда сохранённое значение важно: иначе пин сперва уходит в "выключено",
    // а затем во "включено", и реле щёлкает дважды на каждой загрузке.
    void begin( int pin, bool invertMode, bool initialState = false ){
      this->pin = pin;
      this->invertMode = invertMode;
      this->relayState = initialState;

      // Уровень задаётся ДО перевода пина в выход. pinMode(OUTPUT) выставляет
      // на ножку содержимое защёлки, а она после сброса нулевая -- на плате с
      // активным низким уровнем это кратковременное включение реле. До этого
      // GPIO0 держится внешней подтяжкой в HIGH, иначе ESP-01 не загрузится,
      // то есть реле выключено, и щелчка быть не должно вовсе.
      digitalWrite(pin, levelFor(initialState));
      pinMode(pin, OUTPUT);
    }

    // Смена режима с сохранением логического состояния: меняется полярность
    // выхода, а не то, включено ли реле с точки зрения пользователя.
    void SetInvertMode( bool invertMode ){
      this->invertMode = invertMode;
      SetState(relayState);
    }

    void SetState( bool relayState ){
      digitalWrite(pin, levelFor(relayState));
      this->relayState = relayState;

      if (CallbackHandler) CallbackHandler();
    }

    bool GetState(){ return relayState; }

    void ResetState( ){
      if (GetState())
          this->SetState(false);
      else
          this->SetState(true);
    }

  protected:
    // Уровень на ножке, соответствующий логическому состоянию.
    int levelFor( bool on ) const {
      if (invertMode) return on ? LOW : HIGH;
      return on ? HIGH : LOW;
    }

    int pin;
    bool invertMode;
    bool relayState = false;
    void (*CallbackHandler)() = nullptr;
};
