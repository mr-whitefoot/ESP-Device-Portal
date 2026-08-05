#pragma once

// Единый вход в лог.
//
// До этого заголовка логом был println(): одна строка свободного текста
// сразу в Serial и в кольцевой буфер портала. По строке "MQTT connection
// attempt timed out" нельзя было сказать ни когда это случилось, ни насколько
// это важно, ни к какой подсистеме относится -- принадлежность держалась на
// том, что слово "MQTT" вписано в сам текст.
//
// Формат строки:
//
//   [   12.487] I mqtt connected broker=10.0.1.5:1883
//    \________/ | \__/ \_______________________________/
//      аптайм   |  тег      событие и поля key=value
//             уровень
//
// Аптайм, а не стенное время: часы появляются только после NTP, а самое
// интересное происходит до него. Момент синхронизации печатается отдельной
// строкой (ntp synced time=...), и по ней любой аптайм переводится в
// календарное время. Миллисекунды нужны, чтобы отличить "три строки в одно
// мгновение" от "растянуто на секунду" -- на загрузке это разные истории.
//
// key=value после короткого имени события, а не проза: такую строку одинаково
// читают и глазами, и грепом. Значение с пробелами (имя устройства, SSID)
// ставится в строке последним, чтобы не ломать разбор по пробелу.
//
// Обе точки вывода получают одно и то же. Отдельного порога для портала нет
// намеренно: его кольцо -- 1000 байт, и вместо второго порога периодические
// сообщения живут на уровне D, которого в обычной прошивке нет вовсе.
//
// Заголовок рассчитан на unity-сборку main.cpp и пользуется её глобальными
// объектами (Serial, glog), поэтому подключается после их объявления.

#include <Arduino.h>

#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

// Порог, ниже которого строки не попадают в прошивку вовсе. Задаётся сборкой
// (-D LOG_LEVEL=4 для отладки); по умолчанию в прошивке нет уровня D.
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

namespace corelog {

// Теги подсистем. Общий список нужен затем, чтобы имя подсистемы было полем,
// а не частью текста: макрос принимает имя отсюда, и опечатка становится
// ошибкой компиляции, а не незаметно новым тегом.
namespace tag {
constexpr const char* boot = "boot";
constexpr const char* set  = "set";
constexpr const char* wifi = "wifi";
constexpr const char* mqtt = "mqtt";
constexpr const char* ntp  = "ntp";
constexpr const char* web  = "web";
constexpr const char* ota  = "ota";
constexpr const char* dev  = "dev";
constexpr const char* loop = "loop";
}  // namespace tag

// Ширина колонки тега. Выравнивание стоит четырёх пробелов в строке и окупает
// их тем, что подсистема ищется глазами по позиции, а не вычитыванием.
static const uint8_t TAG_WIDTH = 4;

// toPortal=false оставляет строку только в Serial. Нужно ровно одному месту --
// метрикам loop(), которые печатаются раз в секунду и вытеснили бы из
// килобайтного кольца портала всё остальное.
inline void write(uint8_t level, const char* tag, const String& text,
                  bool toPortal = true) {
  if (level > LOG_LEVEL) return;

  uint32_t ms = millis();
  uint32_t sec = ms / 1000;
  uint32_t frac = ms % 1000;

  String line;
  line.reserve(text.length() + 20);

  // Секунды прижаты вправо в пять знаков: до 27 часов аптайма колонки стоят
  // ровно, дальше строка съезжает на знак за декаду -- это дешевле, чем
  // всегда носить в каждой строке ширину под недельный аптайм.
  line += '[';
  if (sec < 10000) line += ' ';
  if (sec < 1000)  line += ' ';
  if (sec < 100)   line += ' ';
  if (sec < 10)    line += ' ';
  line += sec;
  line += '.';
  if (frac < 100) line += '0';
  if (frac < 10)  line += '0';
  line += frac;
  line += F("] ");

  line += "?EWID"[level <= LOG_LEVEL_DEBUG ? level : 0];
  line += ' ';

  line += tag;
  for (uint8_t i = strlen(tag); i < TAG_WIDTH; i++) line += ' ';
  line += ' ';

  line += text;

  Serial.println(line);
  if (toPortal) glog.println(line);
}

// Для колбэков из библиотек: уровень приходит буквой, потому что общий
// заголовок ради одного перечисления библиотеке знать незачем.
inline void writeChar(char level, const char* tag, const String& text) {
  uint8_t n = LOG_LEVEL_DEBUG;
  if (level == 'E') n = LOG_LEVEL_ERROR;
  else if (level == 'W') n = LOG_LEVEL_WARN;
  else if (level == 'I') n = LOG_LEVEL_INFO;
  write(n, tag, text);
}

}  // namespace corelog

// Тег передаётся коротким именем из corelog::tag, текст -- любым выражением,
// собирающим String.
//
// Проверка уровня стоит ДО вычисления аргумента намеренно: условие
// константное, и компилятор выбрасывает вместе с вызовом всю конкатенацию.
// Отсечённая строка не стоит ни флеша, ни аллокации в куче.
#define LOG_E(TAG, text) \
  do { if (LOG_LEVEL >= LOG_LEVEL_ERROR) \
    corelog::write(LOG_LEVEL_ERROR, corelog::tag::TAG, text); } while (0)

#define LOG_W(TAG, text) \
  do { if (LOG_LEVEL >= LOG_LEVEL_WARN) \
    corelog::write(LOG_LEVEL_WARN, corelog::tag::TAG, text); } while (0)

#define LOG_I(TAG, text) \
  do { if (LOG_LEVEL >= LOG_LEVEL_INFO) \
    corelog::write(LOG_LEVEL_INFO, corelog::tag::TAG, text); } while (0)

#define LOG_D(TAG, text) \
  do { if (LOG_LEVEL >= LOG_LEVEL_DEBUG) \
    corelog::write(LOG_LEVEL_DEBUG, corelog::tag::TAG, text); } while (0)
