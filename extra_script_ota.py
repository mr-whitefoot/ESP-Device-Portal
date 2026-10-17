import gzip
import os
import re
import sys

Import("env")


# Предел размера прошивки по возможности обновления по воздуху. Он зависит от
# размера флеша и разметки, поэтому каждый профиль платы задаёт свой
# custom_ota_max_size в platformio.ini.
#
# Проверяется размер бинарника, а не занятый флеш из отчёта сборки:
# по воздуху передаётся именно он.
OTA_MAX_SIZE = int(env.GetProjectOption("custom_ota_max_size"))


def sketch_area_end():
    """Конец области скетчей: _FS_start из ldscript, приведённый к смещению.

    Берётся из того самого ldscript, которым собран этот образ ($LDSCRIPT_PATH),
    а не константой рядом с custom_ota_max_size: второй источник этой величины
    разъехался бы с разметкой молча. У d1_mini скрипт приходит не из
    board_build.ldscript, а из меню размера флеша в манифесте платы, поэтому
    спрашивать надо окружение, а не BoardConfig. В $LDSCRIPT_PATH лежит одно
    имя файла -- каталог линкер находит через -L, поэтому путь собираем сами.

    Это то же самое, из чего конец области считает ядро в getFreeSketchSpace(),
    и то же, что прошивка отдаёт странице обновления полем FW.e.
    """
    path = os.path.join(
        env.PioPlatform().get_package_dir("framework-arduinoespressif8266"),
        "tools", "sdk", "ld", os.path.basename(env.subst("$LDSCRIPT_PATH")))
    with open(path) as f:
        match = re.search(r"_FS_start\s*=\s*(0x[0-9A-Fa-f]+)", f.read())
    return int(match.group(1), 16) - 0x40200000


def check_ota_headroom(target, source, env):
    # Именно с диска: SCons отдаёт для .bin закешированный размер, а файл
    # создаётся сторонним билдером elf2bin.
    path = target[0].get_abspath()
    size = os.path.getsize(path)
    left = OTA_MAX_SIZE - size

    if size > OTA_MAX_SIZE:
        print(
            "\nОшибка: прошивка %d байт, предел для OTA %d байт (превышение %d).\n"
            "Такой образ не поместится в OTA-область выбранной платы.\n"
            % (size, OTA_MAX_SIZE, -left)
        )
        sys.exit(1)

    print("OTA headroom: %d of %d bytes used (%.1f%%), %d left"
          % (size, OTA_MAX_SIZE, 100.0 * size / OTA_MAX_SIZE, left))

    # Сжатый образ кладём рядом: по воздуху через портал уходит именно он, и
    # локальная сборка должна давать те же файлы, что релизный workflow.
    # mtime=0 -- чтобы у одного и того же образа был один и тот же md5.
    raw = open(path, "rb").read()
    with open(path + ".gz", "wb") as f:
        with gzip.GzipFile(fileobj=f, mode="wb", compresslevel=9, mtime=0) as gz:
            gz.write(raw)
    packed = os.path.getsize(path + ".gz")

    # Вторая граница безопасности сжатого образа. eboot распаковывает от нуля
    # вверх, не проверяя, не догнал ли он собственный источник, а источник
    # лежит с адреса `конец области − round(gz)`. Значит распакованный образ
    # обязан кончиться раньше этого адреса.
    #
    # Первую границу -- что стейджинг вообще влезает за текущий скетч --
    # проверить здесь нельзя: она зависит от того, что сейчас стоит на
    # устройстве. Её считает страница обновления по факту FW.f.
    end = sketch_area_end()
    staged = (packed + 4095) & ~4095
    if size > end - staged:
        print(
            "\nОшибка: сжатый образ %d байт стейджится с адреса %d,\n"
            "а распакованные %d байт дойдут до %d -- eboot затрёт источник.\n"
            % (packed, end - staged, size, size)
        )
        sys.exit(1)

    print("gzip: %d bytes (%.1f%%), unpacks to %d below staging at %d"
          % (packed, 100.0 * packed / size, size, end - staged))


# Этот скрипт подключён как post: после platform builder. На фазе pre target
# .bin ещё не существует в графе SCons, и AddPostAction привязывается не к тому
# узлу: образ создаётся, но проверка размера молча не запускается.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_ota_headroom)


# Фреймворк добавляет в ссылку `-u _printf_float -u _scanf_float`: они тянут
# printf/scanf с плавающей точкой, ~6,9 КБ флеша. У нас они не нужны: printf-
# семейство печатает только %u и %x, String(float) идёт через dtostrf, JSON
# пишет числа сам. build_unflags здесь не годится: "-u" и его аргумент --
# отдельные элементы, и он снимает все "-u", оставляя аргументы сиротами.
# Поэтому снимается ровно эта пара.
_float_io = ("_printf_float", "_scanf_float")
_link, _kept, _i = list(env["LINKFLAGS"]), [], 0
while _i < len(_link):
    if _link[_i] == "-u" and _i + 1 < len(_link) and _link[_i + 1] in _float_io:
        _i += 2
        continue
    _kept.append(_link[_i])
    _i += 1
env.Replace(LINKFLAGS=_kept)
