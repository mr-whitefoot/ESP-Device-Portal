#!/usr/bin/env python3
"""Аппаратный тест реле: портал, MQTT, таймеры, перезагрузка, OTA на живом устройстве.

Автоматизирует ручной smoke-тест из docs/runbooks/verification.md (разделы 4 и
5), всё, что можно проверить по сети. Только стандартная библиотека; MQTT --
тем же голым клиентом, что в tools/mqtt_watch.py.

Адрес устройства в репозитории не хранится и по умолчанию не подставляется:

    python3 tools/hw_test.py --host <адрес>
    python3 tools/hw_test.py --host <адрес> --only portal_relay --only timer
    python3 tools/hw_test.py --host <адрес> --ota-image .pio/build/esp01_relay/ESP_Relay_v4.8.6.bin.gz
    python3 tools/hw_test.py --host <адрес> --destructive    # + смена префикса топиков
    python3 tools/hw_test.py --list
    python3 tools/hw_test.py --self-test                     # без устройства

Брокер -- из tools/broker.ini (в .gitignore, образец broker.ini.example), или
--broker-ini PATH. Файла нет -- проверки MQTT пропускаются (SKIP), не падают.
Авторизация портала -- --user/--password либо HW_TEST_USER/HW_TEST_PASSWORD.

Тест меняет состояние устройства и возвращает его: перед началом снимаются
реле и три формы (Preferences, Timers, MQTT), каждая проверка откатывает своё
в finally, а в конце всё сверяется со снимком ещё раз. Factory reset, Wi-Fi и
префикс топиков (без --destructive) не затрагиваются. Образ датчика и
многоканальный блок тест отвергает.
"""
import argparse
import base64
import gzip
import hashlib
import html.parser
import http.client
import json
import os
import re
import socket
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fleet_update as fleet  # noqa: E402
import mqtt_watch as mw  # noqa: E402

BROKER_INI = Path(__file__).resolve().parent / "broker.ini"


# Сетевой сбой: обрыв, таймаут, разбор ответа http.client.
NET = (OSError, http.client.HTTPException)


class Skip(Exception):
    pass


# --- Чистые помощники (покрыты --self-test) -------------------------------

def topic_segment(name):
    """sanitizeTopicSegment() из lib/mqtt_topics: недопустимое схлопывается в '_'."""
    out = re.sub(r"[^A-Za-z0-9_-]+", "_", name)
    return "ESP" if out in ("", "_") else out


def seconds_of_day(text):
    """'00:25:21' -> секунды от полуночи; None, если время ещё не синхронизировано."""
    m = re.fullmatch(r"(\d\d):(\d\d):(\d\d)", text.strip())
    return m and int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])


def hms(sec):
    sec %= 86400
    return sec // 3600, sec // 60 % 60, sec % 60


def image_info(path, data):
    """Запись образа как в manifest.json: file, size, md5 (и raw для .gz)."""
    info = {"file": os.path.basename(path), "size": len(data),
            "md5": hashlib.md5(data).hexdigest()}
    if path.endswith(".gz"):
        info["raw"] = len(gzip.decompress(data))
    return info


class FormParser(html.parser.HTMLParser):
    """Поля формы так, как их отправил бы браузер: имя -> строка или bool (галка)."""

    def __init__(self):
        super().__init__()
        self.fields, self.action = {}, None
        self._select = None  # [имя, первое значение, выбранное]

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "form":
            self.action = a.get("action")
        elif tag == "select":
            self._select = [a.get("name"), None, None]
        elif tag == "option" and self._select:
            value = a.get("value", "")
            if self._select[1] is None:
                self._select[1] = value
            if "selected" in a:
                self._select[2] = value
        elif tag == "input" and a.get("name") and "disabled" not in a:
            kind, name = a.get("type", "text"), a["name"]
            if kind in ("button", "submit", "radio"):
                return
            if kind == "checkbox":
                self.fields[name] = "checked" in a
            elif kind == "hidden" and isinstance(self.fields.get(name), bool):
                return  # парный hidden=0 у галки
            else:
                self.fields[name] = a.get("value", "")

    def handle_endtag(self, tag):
        if tag == "select" and self._select:
            name, first, chosen = self._select
            self.fields[name] = chosen if chosen is not None else first
            self._select = None


def parse_form(page):
    p = FormParser()
    p.feed(page)
    return p.fields


def encode_form(fields):
    """Тело POST: включённая галка -- on и парный 0, как у браузера."""
    pairs = []
    for name, value in fields.items():
        if value is True:
            pairs += [(name, "on"), (name, "0")]
        elif value is False:
            pairs.append((name, "0"))
        else:
            pairs.append((name, value))
    return urllib.parse.urlencode(pairs).encode()


# --- Устройство -----------------------------------------------------------

class Device:
    """HTTP-сторона: те же запросы, что шлёт страница портала."""

    def __init__(self, host, user=None, password=None):
        self.host = host
        self.headers = {}
        if user:
            token = base64.b64encode(f"{user}:{password or ''}".encode()).decode()
            self.headers["Authorization"] = "Basic " + token

    def http(self, method, path, body=None, timeout=8, headers=None):
        request = urllib.request.Request(
            f"http://{self.host}{path}", data=body, method=method,
            headers={**self.headers, **(headers or {})})
        with urllib.request.urlopen(request, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")

    def page(self, path, **kw):
        for attempt in range(3):  # устройство изредка рвёт chunked-ответ на середине
            try:
                status, text = self.http("GET", path, **kw)
                break
            except http.client.IncompleteRead:
                if attempt == 2:
                    raise
        assert status == 200, f"{path}: HTTP {status}"
        return text

    def facts(self):
        facts = fleet.parse_facts(self.page("/"))
        assert facts, "на главной нет FW={...} (авторизация или чужая прошивка)"
        return facts

    def form(self, path):
        return parse_form(self.page(path))

    def post_form(self, path, fields):
        fields = dict(fields)
        fields.setdefault("GP_form", path)
        self.http("POST", path, encode_form(fields),
                  headers={"Content-Type": "application/x-www-form-urlencoded"})

    def update(self, *ids):
        """GP_update: значения полей через \\x01 в порядке запроса."""
        _, text = self.http("GET", "/GP_update?" + ",".join(ids) + "=")
        return text.split("\x01")

    def switch(self):
        return int(self.update("switch")[0])

    def click(self, name, value=""):
        self.http("POST", f"/GP_click?{name}={value}")

    def set_switch(self, on, timeout=5):
        self.click("switch", int(on))
        wait(lambda: self.switch() == int(on), timeout, f"switch не стал {int(on)}")

    def clock(self):
        sec = seconds_of_day(self.update("time")[0])
        assert sec is not None, "время устройства не синхронизировано (NTP)"
        return sec

    def reboot(self):
        self.click("rebootButton")
        return self.wait_restart()

    def wait_restart(self, down=15, up=90):
        """Ждёт, пока портал пропадёт и вернётся; отдаёт секунды простоя."""
        wait(self._is_down, down, "портал не пропал после перезагрузки", step=0.3)
        gone = time.monotonic()
        wait(self._is_up, up, "портал не вернулся", step=1)
        return time.monotonic() - gone

    def _is_down(self):
        try:
            self.http("GET", "/GP_update?time=", timeout=1.5)
            return False
        except NET:
            return True

    def _is_up(self):
        try:
            return self.facts() is not None
        except NET + (AssertionError,):
            return False


def wait(predicate, timeout, message, step=0.25):
    deadline = time.monotonic() + timeout
    while True:
        try:
            if predicate():
                return
        except NET:
            pass
        if time.monotonic() > deadline:
            raise AssertionError(f"{message} (за {timeout} с)")
        time.sleep(step)


# --- MQTT -----------------------------------------------------------------

class Mqtt:
    """Подписчик и издатель QoS 0 поверх низкоуровневых функций mqtt_watch."""

    def __init__(self, ini):
        # Свой id на каждое соединение: брокер выбрасывает прежнего клиента с тем же id.
        mw.CLIENT_ID = "hw-test-%d-%d" % (os.getpid() % 100000, int(time.monotonic() * 1000) % 100000)
        host, port, user, password = mw.read_config(ini)
        try:
            self.sock = mw.connect(host, port, user, password)
        except SystemExit as e:
            raise AssertionError(f"брокер отказал: {e}")
        self.sock.settimeout(0.5)
        self.events = []  # (топик, payload, retain)
        self._ping = time.monotonic()

    def close(self):
        try:
            self.sock.send(b"\xe0\x00")
            self.sock.close()
        except OSError:
            pass

    def sub(self, topic):
        mw.subscribe(self.sock, topic)

    def pub(self, topic, payload, retain=False):
        body = mw.enc_str(topic) + payload.encode()
        self.sock.send(bytes([0x30 | retain]) + mw.enc_len(len(body)) + body)

    def _exact(self, n):
        data, deadline = b"", time.monotonic() + 5
        while len(data) < n:
            try:
                chunk = self.sock.recv(n - len(data))
            except socket.timeout:
                if time.monotonic() > deadline:
                    raise AssertionError("MQTT: обрыв пакета")
                continue
            if not chunk:
                raise AssertionError("MQTT: брокер закрыл соединение")
            data += chunk
        return data

    def pump(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if time.monotonic() - self._ping > 30:
                self.sock.send(b"\xc0\x00")
                self._ping = time.monotonic()
            try:
                first = self.sock.recv(1)
            except socket.timeout:
                continue
            if not first:
                raise AssertionError("MQTT: брокер закрыл соединение")
            length, mult = 0, 1
            while True:
                byte = self._exact(1)[0]
                length += (byte & 127) * mult
                if not byte & 0x80:
                    break
                mult *= 128
            body = self._exact(length) if length else b""
            if first[0] >> 4 == 3:
                tlen = int.from_bytes(body[:2], "big")
                self.events.append((body[2:2 + tlen].decode(errors="replace"),
                                    body[2 + tlen:].decode(errors="replace"),
                                    first[0] & 1))

    def wait(self, topic, pred, timeout, since=0, live=False):
        """Первый payload топика после индекса since, для которого pred истинно."""
        end = time.monotonic() + timeout
        seen = since
        while True:
            for t, p, r in self.events[seen:]:
                if t == topic and (not live or not r) and pred(p):
                    return p
            seen = len(self.events)
            if time.monotonic() > end:
                return None
            self.pump(0.3)


# --- Контекст и проверки --------------------------------------------------

class Ctx:
    def __init__(self, args):
        self.args = args
        self.dev = Device(args.host, args.user, args.password)
        self.ini = Path(args.broker_ini)
        self._mqtt = None
        self.snap = {}

    def mqtt(self):
        if not self.ini.exists():
            raise Skip(f"нет {self.ini.name}: брокер не настроен")
        if self._mqtt is None:
            self._mqtt = Mqtt(self.ini)
        return self._mqtt

    def drop_mqtt(self):
        if self._mqtt:
            self._mqtt.close()
            self._mqtt = None

    def topics(self, prefix=None):
        prefix = prefix or self.snap["mqtt"]["topicPrefix"]
        root = f"{prefix}/switch/{topic_segment(self.snap['prefs']['deviceName'])}"
        return {k: f"{root}/{k}" for k in ("config", "state", "available", "set")}

    def need_plain_relay(self):
        if self.snap["prefs"].get("relayButtonMode"):
            raise Skip("включён Button mode")


CHECKS = []  # (имя, описание, функция, флаги)


def check(name, doc, destructive=False):
    def reg(fn):
        CHECKS.append((name, doc, fn, destructive))
        return fn
    return reg


@check("pages", "страницы портала отвечают 200 и содержат свои элементы; FW разбирается")
def t_pages(c):
    d = c.dev
    for path, marker in [("/", "Control"), ("/timers", "timerEnable0"), ("/config", "Reboot"),
                         ("/config/preferences", "relayInvertMode"),
                         ("/config/wifi_config", "WiFi configuration"),
                         ("/config/mqtt_config", "topicPrefix"),
                         ("/config/factory_reset", "resetAllow"),
                         ("/ota_update", "firmware_form"), ("/log", "_gplog")]:
        assert marker in d.page(path), f"{path}: нет {marker!r}"
    f = d.facts()
    if c.args.expect_version:
        assert f["v"] == c.args.expect_version, f"версия {f['v']}, ожидалась {c.args.expect_version}"
    return f"v{f['v']} {f['i']} f={f['f']}"


@check("portal_relay", "переключатель портала (GP_click) меняет реле, GP_update и страница видят его")
def t_portal_relay(c):
    c.need_plain_relay()
    d, s0 = c.dev, c.snap["relay"]
    try:
        for want in (1 - s0, s0):
            d.set_switch(want)
            has = "checked" in re.search(r"<input[^>]*name='switch'[^>]*>", d.page("/"))[0]
            assert has == bool(want), "главная страница не отражает реле"
    finally:
        d.set_switch(s0)


@check("log", "лог портала (GP_update log) отдаёт свежие строки о действиях")
def t_log(c):
    c.need_plain_relay()
    d, s0 = c.dev, c.snap["relay"]
    d.update("log")  # выбрать накопленное
    try:
        d.click("switch", 1 - s0)
        wait(lambda: "click switch" in d.update("log")[0], 5, "в логе нет 'click switch'")
        d.update("log")
    finally:
        d.set_switch(s0)
    assert "relay state=" in "".join(d.update("log")), "в логе нет 'relay state='"


@check("invert", "инверсия меняет полярность, но не логическое состояние; настройка сохраняется")
def t_invert(c):
    c.need_plain_relay()
    d, orig = c.dev, c.snap["prefs"]
    s0 = c.snap["relay"]
    try:
        d.post_form("/config/preferences", {**orig, "relayInvertMode": not orig["relayInvertMode"]})
        assert d.form("/config/preferences")["relayInvertMode"] == (not orig["relayInvertMode"])
        assert d.switch() == s0, "инверсия изменила логическое состояние"
    finally:
        d.post_form("/config/preferences", orig)
        d.set_switch(s0)


@check("timer", "суточный таймер срабатывает в заданную секунду и меняет реле")
def t_timer(c):
    c.need_plain_relay()
    d, orig, s0 = c.dev, c.snap["timers"], c.snap["relay"]
    slot = next((i for i in range(5) if not orig[f"timerEnable{i}"]), None)
    if slot is None:
        raise Skip("нет свободного таймера")
    if f"timerAction{slot}" not in orig:
        raise Skip("нет выбора Action (Button mode)")
    try:
        h, m, s = hms(d.clock() + 10)
        want = 1 - s0
        d.post_form("/timers", {**orig, f"timerEnable{slot}": True, f"timerHours{slot}": str(h),
                                f"timerMinutes{slot}": str(m), f"timerSeconds{slot}": str(s),
                                f"timerAction{slot}": "0" if want else "1"})
        wait(lambda: d.switch() == want, 25, "таймер не сработал")
    finally:
        d.post_form("/timers", orig)
        d.set_switch(s0)
    assert d.form("/timers") == orig, "таймеры не вернулись"


@check("persistence", "настройка таймера переживает перезагрузку")
def t_persistence(c):
    d, orig = c.dev, c.snap["timers"]
    slot = next((i for i in range(5) if not orig[f"timerEnable{i}"]), None)
    if slot is None:
        raise Skip("нет свободного таймера")
    try:
        d.post_form("/timers", {**orig, f"timerMinutes{slot}": "37", f"timerSeconds{slot}": "21"})
        d.reboot()
        got = d.form("/timers")
        assert got[f"timerMinutes{slot}"] == "37" and got[f"timerSeconds{slot}"] == "21", \
            "значение не пережило перезагрузку"
    finally:
        d.post_form("/timers", orig)
    assert d.form("/timers") == orig


@check("save_state", "Save relay status: включено -- реле восстанавливается после перезагрузки, выключено -- нет")
def t_save_state(c):
    c.need_plain_relay()
    d, orig, s0 = c.dev, c.snap["prefs"], c.snap["relay"]
    try:
        for save, expect in ((True, 1), (False, 0)):
            d.post_form("/config/preferences", {**orig, "relaySaveStatus": save})
            d.set_switch(1)
            d.reboot()
            assert d.switch() == expect, f"saveState={save}: после перезагрузки реле {d.switch()}, ждали {expect}"
    finally:
        d.post_form("/config/preferences", orig)
        d.set_switch(s0)


@check("mqtt_discovery", "retained discovery корректен, availability online, state содержит switch")
def t_mqtt_discovery(c):
    m, t = c.mqtt(), c.topics()
    m.sub(t["config"].rsplit("/", 1)[0] + "/#")
    m.pump(3)
    got = {k: next((p for tp, p, r in m.events if tp == t[k] and r), None)
           for k in ("config", "state", "available")}
    assert all(v is not None for v in got.values()), f"нет retained: {[k for k, v in got.items() if v is None]}"
    cfg = json.loads(got["config"])
    for key, want in (("avty_t", t["available"]), ("stat_t", t["state"]), ("cmd_t", t["set"]),
                      ("pl_on", "ON"), ("pl_off", "OFF"), ("pl_avail", "online"),
                      ("pl_not_avail", "offline"), ("sw_version", c.dev.facts()["v"])):
        have = cfg.get(key, cfg.get("device", {}).get(key))
        assert have == want, f"config.{key}={have!r}, ждали {want!r}"
    assert cfg.get("uniq_id") and cfg["device"].get("identifiers"), "нет uniq_id/identifiers"
    assert got["available"] == "online", f"available={got['available']!r}"
    assert "switch" in json.loads(got["state"]), "в state нет switch"


@check("mqtt_state", "переключение из портала сразу публикует state в MQTT")
def t_mqtt_state(c):
    c.need_plain_relay()
    m, t, d, s0 = c.mqtt(), c.topics(), c.dev, c.snap["relay"]
    m.sub(t["state"])
    m.pump(1)
    try:
        for want in (1 - s0, s0):
            mark = len(m.events)
            d.set_switch(want)
            assert m.wait(t["state"], lambda p: json.loads(p)["switch"] == bool(want), 5,
                          mark, live=True) is not None, f"state не стал {bool(want)}"
    finally:
        d.set_switch(s0)


@check("mqtt_command", "команды ON/OFF в set меняют реле, мусор игнорируется")
def t_mqtt_command(c):
    c.need_plain_relay()
    m, t, d, s0 = c.mqtt(), c.topics(), c.dev, c.snap["relay"]
    m.sub(t["state"])
    m.pump(1)
    try:
        for want, word in ((1 - s0, "ON" if s0 == 0 else "OFF"), (s0, "ON" if s0 else "OFF")):
            mark = len(m.events)
            m.pub(t["set"], word)
            assert m.wait(t["state"], lambda p: json.loads(p)["switch"] == bool(want), 5,
                          mark, live=True) is not None, f"{word}: state не изменился"
            assert d.switch() == want, f"{word}: портал показывает {d.switch()}"
        mark = len(m.events)
        m.pub(t["set"], "BOGUS")
        m.pump(2)
        # state уходит и по таймеру (Message), поэтому смотрим значение, а не факт.
        assert d.switch() == s0 and all(json.loads(p)["switch"] == bool(s0)
                                        for tp, p, _ in m.events[mark:] if tp == t["state"]), \
            "нераспознанная команда изменила реле"
    finally:
        d.set_switch(s0)


@check("button_mode", "Button mode: команда ON даёт импульс и сама отпускается; таймеры без Action")
def t_button_mode(c):
    c.need_plain_relay()
    m, t, d = c.mqtt(), c.topics(), c.dev
    orig, s0 = c.snap["prefs"], c.snap["relay"]
    m.sub(t["state"])
    m.pump(1)
    try:
        d.post_form("/config/preferences", {**orig, "relayButtonMode": True})
        assert "timerAction0" not in d.page("/timers"), "в Button mode таймеры показывают Action"
        assert "relayPush" in d.page("/"), "на главной нет кнопки Push"
        m.pump(1)
        for how in ("mqtt", "push"):
            mark = len(m.events)
            m.pub(t["set"], "ON") if how == "mqtt" else d.click("relayPush")
            on = m.wait(t["state"], lambda p: json.loads(p)["switch"], 3, mark, live=True)
            off = on and m.wait(t["state"], lambda p: not json.loads(p)["switch"], 3, mark, live=True)
            assert on and off, f"{how}: нет импульса ON -> OFF"
    finally:
        d.post_form("/config/preferences", orig)
        d.set_switch(s0)
    assert "timerAction0" in d.page("/timers"), "после Button mode таймеры без Action"


@check("reboot", "кнопка Reboot: availability offline -> online, портал и реле возвращаются")
def t_reboot(c):
    try:
        m, t = c.mqtt(), c.topics()
        m.sub(t["available"])
        m.pump(1)
    except Skip:
        m = t = None
    mark = len(m.events) if m else 0
    down = c.dev.reboot()
    if m:
        m.pump(1)  # offline и online пришли, пока шла перезагрузка
    msg = f"портал вернулся через {down:.0f} с"
    if m:
        assert any(p == "offline" for tp, p, r in m.events[mark:] if tp == t["available"]), \
            "перед перезагрузкой не было retained offline"
        assert m.wait(t["available"], lambda p: p == "online", 60, mark, live=True), \
            "availability не вернулась в online"
        msg += ", offline -> online"
    return msg


@check("mqtt_prefix", "смена Topic prefix: новый корень объявлен, старый очищен; префикс возвращается",
       destructive=True)
def t_mqtt_prefix(c):
    d, orig = c.dev, c.snap["mqtt"]
    old, new = orig["topicPrefix"], orig["topicPrefix"] + "_hwtest"

    def root_state(prefix):
        c.drop_mqtt()
        m = c.mqtt()
        m.sub(c.topics(prefix)["state"].rsplit("/", 1)[0] + "/#")  # только своё устройство
        m.pump(3)
        return {tp: p for tp, p, r in m.events if r and p}

    def move(src, dst):
        d.post_form("/config/mqtt_config", {**orig, "topicPrefix": dst})
        d.wait_restart()
        c.snap["mqtt"] = {**orig, "topicPrefix": dst}
        wait(lambda: root_state(dst).get(c.topics(dst)["available"]) == "online", 60,
             f"под {dst} нет availability online")
        assert c.topics(dst)["config"] in root_state(dst), "нет discovery под новым корнем"
        try:
            wait(lambda: not root_state(src), 120, f"под {src} остались retained")
        except AssertionError as e:
            left = sorted(t.rsplit("/", 1)[1] for t in root_state(src))
            raise AssertionError(f"{e}: {left}")

    try:
        move(old, new)
        move(new, old)
    finally:
        if c.snap["mqtt"]["topicPrefix"] != old or d.form("/config/mqtt_config")["topicPrefix"] != old:
            d.post_form("/config/mqtt_config", orig)
            d.wait_restart()
        c.snap["mqtt"] = orig
        # Тестовый корень прошивка сама не убирает, если уборка не сработала:
        # снять за собой своими руками (пустой retained), чтобы HA не увидел дубль.
        try:
            c.drop_mqtt()
            m = c.mqtt()
            m.sub(c.topics(new)["state"].rsplit("/", 1)[0] + "/#")
            m.pump(2)
            for tp, p, r in list(m.events):
                if r and p:
                    m.pub(tp, "", True)
            m.pump(1)
        except (Skip, AssertionError, OSError):
            pass


def _image(c):
    path = c.args.ota_image
    if not path:
        raise Skip("не задан --ota-image")
    data = Path(path).read_bytes()
    image = image_info(path, data)
    err = fleet.room_error(image, c.dev.facts())
    assert not err, err
    return image, data


@check("ota_bad_md5", "заливка с неверным md5 отвергается, устройство продолжает работать")
def t_ota_bad(c):
    image, data = _image(c)
    before = c.dev.facts()
    body = fleet.upload(c.args.host, {**image, "md5": "0" * 32}, data, c.dev.headers).decode("utf-8", "replace")
    assert "Update success" not in body and "Update error" in body, f"ответ: {body[:80]!r}"
    time.sleep(4)
    assert c.dev.facts() == before, "устройство после отказа изменилось"
    return "отказ: " + re.sub("<[^>]+>", " ", body).split("Update error")[1].strip()[:40]


@check("ota", "заливка образа через /ota_update: Update success, перезагрузка, новый образ работает")
def t_ota(c):
    image, data = _image(c)
    before = c.dev.facts()
    body = fleet.upload(c.args.host, image, data, c.dev.headers).decode("utf-8", "replace")
    assert "Update success" in body, f"ответ: {body[:80]!r}"
    c.dev.wait_restart()
    after = c.dev.facts()
    # Версия и f вместе: одна версия не доказывает, что образ новый (так уже
    # обманывались). Перезагрузка с возвратом портала подтверждена выше, f
    # сравнивается с прежним и выводится: у того же образа он совпадает.
    m = re.search(r"v(\d+\.\d+\.\d+)", image["file"])
    if m:
        assert after["v"] == m[1], f"версия {after['v']}, образ {m[1]}"
    return f"v{before['v']} f={before['f']} -> v{after['v']} f={after['f']}"


# --- Запуск ---------------------------------------------------------------

def snapshot(c):
    d = c.dev
    c.snap = {"relay": d.switch(), "prefs": d.form("/config/preferences"),
              "timers": d.form("/timers"), "mqtt": d.form("/config/mqtt_config")}
    c.snap0 = json.loads(json.dumps(c.snap))


def restore_all(c):
    """Страховка поверх откатов проверок: форма отличается от снимка -- вернуть."""
    d, notes = c.dev, []
    for key, path in (("prefs", "/config/preferences"), ("timers", "/timers")):
        if d.form(path) != c.snap0[key]:
            d.post_form(path, c.snap0[key])
            notes.append(f"{key} восстановлены")
    if d.form("/config/mqtt_config") != c.snap0["mqtt"]:
        d.post_form("/config/mqtt_config", c.snap0["mqtt"])
        d.wait_restart()
        notes.append("mqtt восстановлен")
    if d.switch() != c.snap0["relay"]:
        d.set_switch(c.snap0["relay"])
        notes.append("реле восстановлено")
    return notes


def run(args):
    c = Ctx(args)
    f = c.dev.facts()
    if f["i"] != "Relay":
        sys.exit(f"Отказ: образ {f['i']!r}, тест только для одноканального реле ('Relay')")
    snapshot(c)
    print(f"Устройство: v{f['v']} {f['i']}, реле={c.snap['relay']}, "
          f"имя={c.snap['prefs']['deviceName']!r}, префикс={c.snap['mqtt']['topicPrefix']!r}")

    wanted = args.only or [n for n, _, _, dest in CHECKS if not dest or args.destructive]
    failed = 0
    try:
        for name, doc, fn, dest in CHECKS:
            if name not in wanted:
                continue
            if dest and not args.destructive:
                print(f"SKIP  {name:<15} нужен --destructive")
                continue
            t0 = time.monotonic()
            try:
                detail = fn(c) or ""
                status = "PASS"
            except Skip as e:
                status, detail = "SKIP", str(e)
            except (AssertionError, ValueError, KeyError) + NET as e:
                status, detail = "FAIL", f"{type(e).__name__}: {e}"
                failed += 1
            print(f"{status}  {name:<15} {time.monotonic() - t0:6.1f} с  {detail}", flush=True)
            c.drop_mqtt()
            c.snap = json.loads(json.dumps(c.snap0))
    finally:
        c.drop_mqtt()
        try:
            notes = restore_all(c)
            final = {"relay": c.dev.switch(), "prefs": c.dev.form("/config/preferences"),
                     "timers": c.dev.form("/timers"), "mqtt": c.dev.form("/config/mqtt_config")}
            same = "совпадает" if final == c.snap0 else "НЕ СОВПАДАЕТ"
            print(f"Итог состояния: реле {c.snap0['relay']} -> {final['relay']}, "
                  f"настройки {same}" + (f" ({'; '.join(notes)})" if notes else ""))
            failed += final != c.snap0
        except NET + (AssertionError,) as e:
            print(f"Восстановление не удалось: {e}")
            failed += 1
    print(f"{'ПРОВАЛ' if failed else 'ОК'}: {failed} FAIL")
    return 1 if failed else 0


def self_test():
    assert topic_segment("ESP Relay") == "ESP_Relay" and topic_segment("Ратан") == "ESP"
    assert topic_segment("a b!!c") == "a_b_c" and topic_segment("") == "ESP" and topic_segment("!!") == "ESP"
    assert seconds_of_day("00:25:21") == 1521 and seconds_of_day("") is None
    assert hms(86395 + 10) == (0, 0, 5)
    page = ("<form action='/x' method='POST'><input type='hidden' name='GP_form' value='/x'>"
            "<input type='text' name='deviceName' value='Rattan &amp; Co'>"
            "<input type='checkbox' name='a' checked><input type='hidden' value='0' name='a'>"
            "<input type='checkbox' name='b'><input type='hidden' value='0' name='b'>"
            "<input type='password' name='pw' value=''><input type='radio' disabled checked name='led'>"
            "<select name='h'><option value='0'>00<option value='1' selected>01</select>"
            "<select name='m'><option value='5'>05<option value='6'>06</select>"
            "<input type='submit' value='Save'></form>")
    fields = parse_form(page)
    assert fields == {"GP_form": "/x", "deviceName": "Rattan & Co", "a": True, "b": False,
                      "pw": "", "h": "1", "m": "5"}, fields
    assert encode_form({"a": True, "b": False, "h": "1"}) == b"a=on&a=0&b=0&h=1"
    info = image_info("x/ESP_Relay_v1.2.3.bin.gz", gzip.compress(b"abc" * 100))
    assert info["file"] == "ESP_Relay_v1.2.3.bin.gz" and info["raw"] == 300
    assert info["md5"] == hashlib.md5(gzip.compress(b"abc" * 100)).hexdigest()
    assert "raw" not in image_info("a.bin", b"1234") and image_info("a.bin", b"1234")["size"] == 4
    print("self-test ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--host", help="адрес устройства (обязателен, по умолчанию нет)")
    p.add_argument("--user", default=os.environ.get("HW_TEST_USER"))
    p.add_argument("--password", default=os.environ.get("HW_TEST_PASSWORD"))
    p.add_argument("--broker-ini", default=str(BROKER_INI), metavar="PATH")
    p.add_argument("--expect-version", metavar="X.Y.Z")
    p.add_argument("--ota-image", metavar="PATH", help=".bin или .bin.gz для проверок OTA")
    p.add_argument("--destructive", action="store_true", help="добавить смену Topic prefix (с возвратом)")
    p.add_argument("--only", action="append", metavar="NAME", help="только эти проверки; можно повторять")
    p.add_argument("--list", action="store_true")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test()
    if args.list:
        for name, doc, _, dest in CHECKS:
            print(f"{name:<15} {doc}" + ("  [--destructive]" if dest else ""))
        return 0
    if not args.host:
        p.error("нужен --host")
    unknown = set(args.only or []) - {n for n, _, _, _ in CHECKS}
    if unknown:
        p.error(f"нет проверок: {', '.join(sorted(unknown))}")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
