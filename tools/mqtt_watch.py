#!/usr/bin/env python3
"""Наблюдатель MQTT: показывает топик, флаг retain и полезную нагрузку.

Зачем свой, а не mosquitto_sub: нужен именно флаг retain. Обычные утилиты
его в выводе не показывают, а проверять приходится как раз его -- состояние
и availability публикуются retained, и без этого поля не отличить сообщение
из хранилища брокера от только что пришедшего.

Зависимостей нет намеренно: голый сокет и MQTT 3.1.1 руками. Ставить paho
или mosquitto-clients ради одной проверки незачем, а на чужой машине
скрипт заработает сразу.

Настройки берутся из tools/broker.ini (в .gitignore, там пароль):

    [broker]
    host = 10.0.1.5
    port = 1883
    username = mqtt
    password = ...

Запуск:

    python3 tools/mqtt_watch.py                             # всё автообнаружение
    python3 tools/mqtt_watch.py 'homeassistant/switch/ESP_Relay/#' 30

Первый аргумент -- фильтр топиков, второй -- сколько секунд слушать.
Сообщения, пришедшие сразу после подписки, брокер отдаёт из хранилища с
retain=1; у живых публикаций флаг нулевой, это не ошибка, а спецификация.
"""
import configparser
import os
import socket
import sys
import time

CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "broker.ini")

TOPIC = sys.argv[1] if len(sys.argv) > 1 else "homeassistant/#"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
CLIENT_ID = "mqtt-watch-%d" % (os.getpid() % 100000)


def read_config():
    if not os.path.exists(CONFIG):
        sys.exit("Нет %s -- скопируйте broker.ini.example и впишите свой брокер"
                 % CONFIG)
    parser = configparser.ConfigParser()
    parser.read(CONFIG)
    b = parser["broker"]
    return b.get("host"), b.getint("port", fallback=1883), \
        b.get("username", ""), b.get("password", "")


def enc_len(n):
    """Remaining Length: семь бит на байт, восьмой -- признак продолжения."""
    out = b""
    while True:
        byte = n % 128
        n //= 128
        out += bytes([byte | (0x80 if n > 0 else 0)])
        if n == 0:
            return out


def enc_str(s):
    data = s.encode()
    return len(data).to_bytes(2, "big") + data


def connect(host, port, user, password):
    sock = socket.create_connection((host, port), timeout=10)
    flags = 0x02  # clean session
    payload = enc_str(CLIENT_ID)
    if user:
        flags |= 0x80
        payload += enc_str(user)
    if password:
        flags |= 0x40
        payload += enc_str(password)
    var = enc_str("MQTT") + bytes([4, flags]) + (60).to_bytes(2, "big")
    sock.send(bytes([0x10]) + enc_len(len(var + payload)) + var + payload)

    hdr = sock.recv(4)
    if len(hdr) < 4 or hdr[0] != 0x20 or hdr[3] != 0:
        sys.exit("Брокер отказал в подключении: %r" % (hdr,))
    return sock


def subscribe(sock, topic):
    var = (1).to_bytes(2, "big") + enc_str(topic) + bytes([0])  # qos 0
    sock.send(bytes([0x82]) + enc_len(len(var)) + var)


def read_len(sock):
    mult, value = 1, 0
    while True:
        byte = sock.recv(1)[0]
        value += (byte & 127) * mult
        if not byte & 0x80:
            return value
        mult *= 128


def main():
    host, port, user, password = read_config()
    sock = connect(host, port, user, password)
    subscribe(sock, TOPIC)
    print("%s:%d -- подписан на %s, слушаю %.0f с\n" %
          (host, port, TOPIC, SECONDS))

    sock.settimeout(1.0)
    deadline = time.time() + SECONDS
    while time.time() < deadline:
        try:
            first = sock.recv(1)
        except socket.timeout:
            continue
        if not first:
            break

        packet_type, retain = first[0] >> 4, first[0] & 1
        length = read_len(sock)
        body = b""
        while len(body) < length:
            body += sock.recv(length - len(body))

        if packet_type != 3:  # интересует только PUBLISH
            continue

        tlen = int.from_bytes(body[:2], "big")
        topic = body[2:2 + tlen].decode(errors="replace")
        payload = body[2 + tlen:].decode(errors="replace")
        print("%s  retain=%d  %s\n    %s\n" %
              (time.strftime("%H:%M:%S"), retain, topic, payload))

    sock.close()


main()
