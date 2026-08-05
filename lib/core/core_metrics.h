#pragma once

// Метрики отзывчивости loop(). Включаются флагом -D LOOP_METRICS, в обычную
// прошивку не попадают ни кодом, ни временем.
//
// Каждая найденная до сих пор просадка портала оказывалась блокировкой внутри
// одного конкретного вызова loop(): опрос готовности датчика, update() у
// NTPClient, резолв имени NTP. Общее "портал тормозит" в поиске не помогало --
// три версии подряд, выведенные из чтения исходников, оказались мимо, а
// причину нашло наблюдение за железом. Поэтому меряется не проход целиком, а
// каждый вызов отдельно: нужно имя вставшего этапа, а не факт задержки.
//
// Заголовок рассчитан на unity-сборку main.cpp, поэтому переменные обычные,
// без inline: трансляционная единица одна.

#ifdef LOOP_METRICS

namespace metrics {

enum StageId : uint8_t {
  ST_OTA, ST_SETTINGS, ST_MQTT, ST_PUBLISH,
  ST_PORTAL, ST_WIFI, ST_NTP, ST_DEVICE, ST_COUNT
};

const char* const STAGE_NAME[ST_COUNT] = {
  "ota", "settings", "mqtt", "publish", "portal", "wifi", "ntp", "device"
};

uint32_t stageWorstUs[ST_COUNT];
uint32_t iterations;
uint32_t worstLoopUs;
uint32_t windowStartMs;
uint32_t quietWindows;

// Окно отчёта. Секунда достаточно мала, чтобы просадка не размазалась по
// длинному интервалу, и достаточно велика, чтобы сама печать не мешала работе.
const uint32_t WINDOW_MS = 1000;

// Ниже этого порога окно считается спокойным. Ненагруженный проход занимает
// десятки микросекунд, и печатать такие окна подробно значит утопить в них
// то единственное, ради чего всё затевалось.
const uint32_t REPORT_THRESHOLD_US = 20000;

// Раз в столько спокойных окон строка печатается всё равно: молчание в
// терминале должно отличаться от зависшего устройства, а число проходов в
// секунду само по себе показывает, голодает loop() или нет.
const uint32_t HEARTBEAT_WINDOWS = 10;

inline void resetWindow() {
  for (uint8_t i = 0; i < ST_COUNT; i++) stageWorstUs[i] = 0;
  iterations = 0;
  worstLoopUs = 0;
}

inline void account(uint8_t id, uint32_t us) {
  if (us > stageWorstUs[id]) stageWorstUs[id] = us;
}

// Спокойное окно уходит только в Serial (toPortal=false): таких окон
// большинство, а кольцевой буфер портала держит всего 1000 байт и был бы
// вытеснен ими целиком.
//
// Окно с просадкой идёт в оба места. Это делает диагностику доступной по
// HTTP, без подключения к serial: таких строк примерно одна на десять секунд,
// и буфер их выдерживает. Ради этого сборка и сделана отдельной: в обычной
// прошивке ничего этого нет.
inline void report(bool loud) {
  String line = "iters=";
  line += iterations;
  line += " worst=";
  line += worstLoopUs;
  line += "us";

  if (!loud) {
    corelog::write(LOG_LEVEL_INFO, corelog::tag::loop, line, false);
    return;
  }

  // Печатается только то, что реально отняло время: этап, простоявший ноль,
  // в строке лишь мешает глазами искать виновника.
  for (uint8_t i = 0; i < ST_COUNT; i++) {
    if (!stageWorstUs[i]) continue;
    line += ' ';
    line += STAGE_NAME[i];
    line += '=';
    line += stageWorstUs[i];
  }

  LOG_I(loop, line);
}

// Разность беззнаковых величин верна и через переполнение micros() (каждые
// ~71 минуту), поэтому отдельной защиты от него не нужно.
inline void endIteration(uint32_t startUs) {
  uint32_t spent = micros() - startUs;
  if (spent > worstLoopUs) worstLoopUs = spent;
  iterations++;

  uint32_t now = millis();
  if (now - windowStartMs < WINDOW_MS) return;
  windowStartMs = now;

  bool loud = worstLoopUs >= REPORT_THRESHOLD_US;
  if (loud) {
    quietWindows = 0;
  } else {
    quietWindows++;
  }

  if (loud || quietWindows >= HEARTBEAT_WINDOWS) {
    report(loud);
    if (!loud) quietWindows = 0;
  }
  resetWindow();
}

}  // namespace metrics

// Вызов остаётся на своём месте в loop() и читается как обычный: замер
// оборачивает его, а не переписывает порядок.
#define STAGE(id, call) \
  do { uint32_t _t0 = micros(); call; metrics::account(metrics::id, micros() - _t0); } while (0)

#define LOOP_METRICS_BEGIN() uint32_t _loopStart = micros()
#define LOOP_METRICS_END()   metrics::endIteration(_loopStart)

#else

// Без флага метрик макросы вырождаются в сам вызов, то есть обычная прошивка
// не платит за них ничем.
#define STAGE(id, call) call
#define LOOP_METRICS_BEGIN() do {} while (0)
#define LOOP_METRICS_END()   do {} while (0)

#endif
