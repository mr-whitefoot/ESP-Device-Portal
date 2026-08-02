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
    void begin( int pin, bool invertMode ){
      this->pin = pin;
      this->invertMode = invertMode;
      pinMode(pin, OUTPUT);
      SetState(false);
    }

    // Смена режима с сохранением логического состояния: меняется полярность
    // выхода, а не то, включено ли реле с точки зрения пользователя.
    void SetInvertMode( bool invertMode ){
      this->invertMode = invertMode;
      SetState(relayState);
    }

    void SetState( bool relayState ){
      if (relayState){
        if (invertMode == true) digitalWrite(pin, LOW );
        else digitalWrite(pin, HIGH );
        this->relayState = true; }
      else{
        if (invertMode == true) digitalWrite(pin, HIGH );
        else digitalWrite(pin, LOW );
        this->relayState = false; }

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
    int pin;
    bool invertMode;
    bool relayState = false;
    void (*CallbackHandler)() = nullptr;
};


class ESPRelayButton: public ESPRelay{
  public:
    void SetState( bool relayState ){
      if (relayState){
        if (invertMode == true) digitalWrite(pin, LOW );
        else digitalWrite(pin, HIGH );

        this->relayState = true;
        long now = millis();
        ButtonOn = now;
      }
      else{
        if (invertMode == true) digitalWrite(pin, HIGH );
        else digitalWrite(pin, LOW );
        this->relayState = false; }

      if (*CallbackHandler) CallbackHandler(); 
    }
    
    bool tick( ){
      long now = millis();
      if ( relayState == true && (now - ButtonOn) > ButtonClick ){
          this->SetState(false);
          return true; }
      else return false; 
    }

  private:
      long ButtonOn = 0;
      int ButtonClick = 500;
};
