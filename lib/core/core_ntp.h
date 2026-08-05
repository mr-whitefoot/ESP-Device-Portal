#pragma once

// Обвязка часов NTP: сеть и только сеть. Решения принимает ntp::NtpClock,
// здесь лишь исполняются его действия -- та же граница, что у автомата WiFi.
//
// Ни один вызов отсюда не ждёт ответа. Прежний NTPClient ждал: замеры на
// железе показывали 1023 мс внутри update(), то есть секунду, в которую
// устройство не обслуживало ни портал, ни OTA.

#include <WiFiUdp.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <ntp_clock.h>

namespace corentp {

const char* const POOL = "pool.ntp.org";

// Порт назначения фиксирован стандартом, локальный выбран произвольно из
// непривилегированных: слушать нужно ровно ответы на наши же запросы.
static const uint16_t NTP_PORT = 123;
static const uint16_t LOCAL_PORT = 8888;

// Между эпохой NTP (1900) и эпохой Unix (1970) лежит 70 лет с 17 високосными.
static const uint32_t SEVENTY_YEARS = 2208988800UL;

static const uint8_t PACKET_SIZE = 48;


ntp::NtpClock clock_;

// Нужен разбору ответа выше по файлу: первая удачная синхронизация печатает
// стенное время, и это единственная строка, по которой аптайм в логе
// переводится в календарное время.
String formattedTime();

WiFiUDP udp;
bool udpStarted = false;

IPAddress serverIp;

// Результат резолва приходит из колбэка lwIP, то есть не из loop(). Отсюда
// volatile: между записью в колбэке и чтением в tick() нет точки синхронизации,
// и компилятору незачем считать, что значения не меняются сами.
volatile bool resolveDone = false;
volatile bool resolveOk = false;
ip_addr_t resolvedAddr;


// Колбэк lwIP. Выполняется в контексте стека, поэтому здесь нельзя ни ждать,
// ни лезть в сеть -- только записать результат и выйти.
void onDnsResult(const char* name, const ip_addr_t* addr, void* arg) {
  (void)name;
  (void)arg;
  if (addr) {
    resolvedAddr = *addr;
    resolveOk = true;
  }
  resolveDone = true;
}


// Асинхронный резолв. WiFi.hostByName для этого не годится: он ждёт ответа
// на месте, то есть возвращает ровно ту блокировку, ради устранения которой
// всё и затевалось.
void startResolve() {
  resolveDone = false;
  resolveOk = false;

  err_t err = dns_gethostbyname(POOL, &resolvedAddr, onDnsResult, nullptr);

  if (err == ERR_OK) {
    // Имя нашлось в кэше lwIP, колбэка не будет -- отвечаем сами.
    resolveOk = true;
    resolveDone = true;
  } else if (err != ERR_INPROGRESS) {
    resolveDone = true;  // resolveOk остаётся ложью
  }
}


void sendRequest() {
  if (!udpStarted) {
    udp.begin(LOCAL_PORT);
    udpStarted = true;
  }

  uint8_t packet[PACKET_SIZE] = {0};
  // LI = 0 (нет предупреждения), VN = 4, Mode = 3 (клиент).
  packet[0] = 0b00100011;

  udp.beginPacket(serverIp, NTP_PORT);
  udp.write(packet, PACKET_SIZE);
  udp.endPacket();
}


// Разбор ответа. Пакет короче положенного отбрасывается: доверять обрезанному
// заголовку значит принять чужой мусор за время.
void pollResponse() {
  int size = udp.parsePacket();
  if (size < PACKET_SIZE) {
    if (size > 0) udp.flush();
    return;
  }

  uint8_t packet[PACKET_SIZE];
  udp.read(packet, PACKET_SIZE);

  // Секунды эпохи NTP лежат в Transmit Timestamp, байты 40..43, big-endian.
  uint32_t ntpSeconds = ((uint32_t)packet[40] << 24) |
                        ((uint32_t)packet[41] << 16) |
                        ((uint32_t)packet[42] << 8)  |
                         (uint32_t)packet[43];

  // Ноль означает неустановленное время на самом сервере (Kiss-of-Death и
  // прочие вырожденные ответы). Такой ответ хуже отсутствия ответа.
  if (ntpSeconds < SEVENTY_YEARS) return;

  bool wasSet = clock_.isTimeSet();
  clock_.onResponse(millis(), ntpSeconds - SEVENTY_YEARS);

  // Только первая синхронизация. Последующие приходят по расписанию каждые
  // несколько минут, и печатать их значит забить ими весь лог; расхождение
  // хода при этом всё равно не видно -- часы правятся молча.
  if (!wasSet) LOG_I(ntp, String(F("synced time=")) + formattedTime());
}


void setOffsetFromSettings(int32_t seconds) {
  clock_.setOffset(seconds);
}


void tick() {
  uint32_t now = millis();
  bool linkUp = (WiFi.status() == WL_CONNECTED);

  // Ответ подбирается в каждом проходе: это дешёвая проверка буфера сокета,
  // а не обращение к сети.
  if (udpStarted && linkUp) pollResponse();

  if (resolveDone) {
    resolveDone = false;
    if (resolveOk) {
      serverIp = IPAddress(ip_addr_get_ip4_u32(&resolvedAddr));
      LOG_I(ntp, String(F("resolved ")) + POOL + "=" + serverIp.toString());
    } else {
      // Неудачный резолв означает, что времени не будет, а без него молча
      // не сработают суточные таймеры реле. Раньше эта ветка не печатала
      // ничего, и отсутствие срабатываний выглядело поломкой таймеров.
      LOG_W(ntp, String(F("resolve failed name=")) + POOL);
    }
    clock_.onResolved(now, resolveOk);
  }

  switch (clock_.tick(now, linkUp)) {
    case ntp::Action::Resolve: startResolve(); break;
    case ntp::Action::Send:    sendRequest();  break;
    case ntp::Action::Nothing: break;
  }
}


bool isTimeSet()        { return clock_.isTimeSet(); }
uint8_t hours()         { return clock_.hours(millis()); }
uint8_t minutes()       { return clock_.minutes(millis()); }
uint8_t seconds()       { return clock_.seconds(millis()); }

String formattedTime() {
  char buf[9];
  clock_.formatTime(millis(), buf);
  return String(buf);
}

}  // namespace corentp
