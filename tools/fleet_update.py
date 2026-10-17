#!/usr/bin/env python3
"""Обновление парка до последнего релиза с компьютера, как кнопка Install.

Делает то же, что web/fw.js, но с машины, у которой есть TLS: читает
manifest.json зеркала, скачивает .bin.gz своего образа, сверяет md5 и шлёт
его на /ota_update?size=&md5= тем же multipart-полем firmware. Границы
безопасности те же, что в fw.js (свободное место f и конец области скетчей e).

Инвентарь -- тот же tools/fleet.ini (в Git не входит, образец рядом:
fleet.ini.example); из него нужен только host. Версию и имя образа
устройство сообщает само на главной странице (FW={v,i,f,e}).

Без --execute печатается только план. Датчики (образ DS18B20*) обновляются
лишь с явным --allow-sensors. Устройства идут по очереди.

    python3 tools/fleet_update.py                        # план для всех
    python3 tools/fleet_update.py --device relay_esp01_1 --execute
    python3 tools/fleet_update.py --self-test            # проверка логики
"""
import argparse
import configparser
import hashlib
import json
import re
import sys
import time
import urllib.request
import uuid
from pathlib import Path

BASE = "https://mr-whitefoot.github.io/ESP-Device-Portal/"
FLEET_INI = Path(__file__).resolve().parent / "fleet.ini"


def parse_facts(html):
    """FW={v:'4.8.3',i:'Relay',f:483328,e:962560} -> словарь или None."""
    m = re.search(r"FW=\{v:'([^']*)',i:'([^']*)',f:(\d+),e:(\d+)\}", html)
    return m and {"v": m[1], "i": m[2], "f": int(m[3]), "e": int(m[4])}


def newer(a, b):
    """Как newer() в fw.js: три числа, не строки ('4.10.0' > '4.9.0')."""
    def key(s):
        return [int(x) if x.isdigit() else 0 for x in (s.split(".") + ["0"] * 3)[:3]]
    return key(a) > key(b)


def pick(manifest, facts):
    """(версия, запись образа) последнего релиза; запись None, если образа нет."""
    version = manifest["version"]
    release = next((r for r in manifest.get("history", []) if r["version"] == version), manifest)
    return version, release["images"].get(facts["i"])


def room_error(image, facts):
    """Две границы fw.js; None, если места хватает. У несжатой записи raw нет."""
    staged = (image["size"] + 4095) & -4096
    raw = image.get("raw", 0)
    if staged > facts["f"] or raw > facts["e"] - staged:
        return f"нет места: {staged}+{raw} при f={facts['f']}, e={facts['e']}"
    return None


def get(url, timeout=10):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()


def read_facts(host):
    return parse_facts(get(f"http://{host}/", 5).decode("utf-8", "ignore"))


def upload(host, image, data, headers=None):
    """POST как в fw.js: size и md5 в строке запроса, файл в поле firmware.
    Возвращает тело ответа устройства (Update success / Update error);
    headers -- дополнительные заголовки, например Authorization."""
    boundary = uuid.uuid4().hex
    head = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; '
            f'filename="{image["file"]}"\r\nContent-Type: application/octet-stream\r\n\r\n')
    body = head.encode() + data + f"\r\n--{boundary}--\r\n".encode()
    request = urllib.request.Request(
        f"http://{host}/ota_update?size={image['size']}&md5={image['md5']}",
        data=body, method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}", **(headers or {})})
    return urllib.request.urlopen(request, timeout=120).read()


def wait_version(host, version, timeout):
    time.sleep(5)  # устройство ещё может отвечать старой прошивкой
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            facts = read_facts(host)
            if facts and facts["v"] == version:
                return True
        except OSError:
            pass
        time.sleep(2)
    return False


def update(host, manifest, args):
    """(сообщение, успех) для одного устройства."""
    try:
        facts = read_facts(host)
    except OSError as error:
        return f"недоступно ({error})", False
    if not facts:
        return "на главной странице нет FW (авторизация или чужая прошивка)", False
    version, image = pick(manifest, facts)
    if not newer(version, facts["v"]):
        return f"актуально ({facts['v']})", True
    if image is None:
        return f"в релизе {version} нет образа {facts['i']}", False
    if facts["i"].startswith("DS18B20") and not args.allow_sensors:
        return f"датчик {facts['i']}: нужен --allow-sensors", True
    error = room_error(image, facts)
    if error:
        return f"отказ: {error}", False
    plan = f"{facts['v']} -> {version} ({image['file']})"
    if not args.execute:
        return f"план: {plan}", True
    data = get(BASE + image["file"], 120)
    if hashlib.md5(data).hexdigest() != image["md5"] or len(data) != image["size"]:
        return "скачанный образ не совпал с манифестом (md5/размер)", False
    try:
        upload(host, image, data)
    except OSError as error:
        # Обрыв после полной отправки (перезагрузка) не отличить от сбоя;
        # решает проверка версии ниже.
        print(f"  ответ не получен ({error}), проверяю версию")
    if not wait_version(host, version, args.verify_timeout):
        return f"{plan}: версия {version} не подтверждена", False
    return f"обновлено: {plan}", True


def self_test():
    facts = parse_facts("<script>FW={v:'4.8.3',i:'Relay',f:483328,e:962560}</script>")
    assert facts == {"v": "4.8.3", "i": "Relay", "f": 483328, "e": 962560}
    assert parse_facts("<html>") is None
    assert newer("4.10.0", "4.9.0") and not newer("4.8.3", "4.8.3") and newer("4.8.4", "4.8.3")
    manifest = {"version": "4.8.4", "images": {"Relay": {"file": "top"}}, "history": [
        {"version": "4.8.4", "images": {"Relay": {"file": "new", "size": 329313, "raw": 476560}}},
        {"version": "4.8.3", "images": {}}]}
    assert pick(manifest, facts)[1]["file"] == "new"
    assert pick(manifest, dict(facts, i="Nope"))[1] is None
    assert pick({"version": "4.7.0", "images": {"Relay": {"file": "old"}}}, facts)[1]["file"] == "old"
    image = manifest["history"][0]["images"]["Relay"]
    assert room_error(image, facts) is None
    assert room_error(image, dict(facts, f=100000))   # не влезает в свободное
    assert room_error(image, dict(facts, e=500000))   # распаковка догонит источник
    assert room_error({"size": 5000}, facts) is None  # несжатая запись: второй проверки нет
    print("self-test ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--inventory", type=Path, default=FLEET_INI, help="fleet.ini")
    p.add_argument("--device", action="append", help="секция инвентаря; можно повторять")
    p.add_argument("--execute", action="store_true", help="реально обновлять; без флага только план")
    p.add_argument("--allow-sensors", action="store_true", help="разрешить обновление DS18B20")
    p.add_argument("--verify-timeout", type=int, default=90, metavar="SECONDS")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test()

    config = configparser.RawConfigParser()
    if not config.read(args.inventory, encoding="utf-8"):
        sys.exit(f"Ошибка: не найден {args.inventory}; см. tools/fleet.ini.example")
    names = args.device or config.sections()
    manifest = json.loads(get(BASE + "manifest.json"))
    print(f"Последний релиз: {manifest['version']}"
          + ("" if args.execute else " (план, --execute не задан)"))
    failed = 0
    for name in names:
        message, ok = update(config[name]["host"].strip(), manifest, args)
        print(f"[{name}] {message}")
        failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
