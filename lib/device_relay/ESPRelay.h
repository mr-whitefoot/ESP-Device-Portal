#include <Arduino.h>
class ESPRelay{
  public:
    static constexpr uint32_t BUTTON_PRESS_MS = 500;

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
    void begin( int pin, bool invertMode, bool initialState = false,
                bool buttonMode = false ){
      this->pin = pin;
      this->invertMode = invertMode;
      this->buttonMode = buttonMode;

      // Импульсный режим никогда не восстанавливает нажатие после загрузки:
      // это было бы защёлкиванием до первого tick(), то есть ровно тем, от
      // чего Button mode должен защищать.
      this->relayState = buttonMode ? false : initialState;

      // Уровень задаётся ДО перевода пина в выход. pinMode(OUTPUT) выставляет
      // на ножку содержимое защёлки, а она после сброса нулевая -- на плате с
      // активным низким уровнем это кратковременное включение реле. До этого
      // GPIO0 держится внешней подтяжкой в HIGH, иначе ESP-01 не загрузится,
      // то есть реле выключено, и щелчка быть не должно вовсе.
      digitalWrite(pin, levelFor(this->relayState));
      pinMode(pin, OUTPUT);
    }

    // Смена режима с сохранением логического состояния: меняется полярность
    // выхода, а не то, включено ли реле с точки зрения пользователя.
    void SetInvertMode( bool invertMode ){
      this->invertMode = invertMode;
      SetState(relayState);
    }

    // При переходе из обычного режима в импульсный уже включённое реле
    // отпускается сразу. Иначе настройка Button mode могла бы оставить
    // нагрузку защёлкнутой до следующей команды.
    void SetButtonMode( bool buttonMode ){
      if (this->buttonMode == buttonMode) return;

      this->buttonMode = buttonMode;
      if (buttonMode && relayState) SetState(false);
    }

    void SetState( bool relayState ){
      bool changed = (this->relayState != relayState);

      // Повторная команда ON начинает полусекундный импульс заново. Это
      // соответствует удержанию кнопки и не вызывает лишний колбэк, потому
      // что логическое состояние при этом не меняется.
      if (buttonMode && relayState) buttonPressedAt = millis();

      // Запись в пин безусловна намеренно: уровень зависит не только от
      // состояния, но и от инверсии, а SetInvertMode() переприменяет ровно
      // то же состояние с новой полярностью.
      digitalWrite(pin, levelFor(relayState));
      this->relayState = relayState;

      // А вот колбэк -- только на фактическое изменение. Он публикует
      // состояние в MQTT и пишет флеш, то есть стоит и эфира, и ресурса
      // памяти. Повторная команда из HomeAssistant приходит при каждом
      // нажатии, а SetInvertMode() дёргал его на каждом сохранении формы
      // Preferences -- и это уже вылезало на железе лишним state в брокере.
      if (changed && CallbackHandler) CallbackHandler();
    }

    bool GetState(){ return relayState; }

    bool GetButtonMode(){ return buttonMode; }

    // Неблокирующее отпускание кнопки. Вызывающий обязан звать Tick() из
    // loop(); вычитание без сравнения абсолютных времён корректно работает
    // и при переполнении millis().
    void Tick(){
      if (buttonMode && relayState &&
          (uint32_t)(millis() - buttonPressedAt) >= BUTTON_PRESS_MS)
        SetState(false);
    }

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
    bool buttonMode = false;
    bool relayState = false;
    uint32_t buttonPressedAt = 0;
    void (*CallbackHandler)() = nullptr;
};
