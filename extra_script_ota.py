import os
import sys

Import("env")


# Предел размера прошивки по возможности обновления по воздуху. Он зависит от
# размера флеша и разметки, поэтому каждый профиль платы задаёт свой
# custom_ota_max_size в platformio.ini.
#
# Проверяется размер бинарника, а не занятый флеш из отчёта сборки:
# по воздуху передаётся именно он.
OTA_MAX_SIZE = int(env.GetProjectOption("custom_ota_max_size"))


def check_ota_headroom(target, source, env):
    # Именно с диска: SCons отдаёт для .bin закешированный размер, а файл
    # создаётся сторонним билдером elf2bin.
    size = os.path.getsize(target[0].get_abspath())
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


# Этот скрипт подключён как post: после platform builder. На фазе pre target
# .bin ещё не существует в графе SCons, и AddPostAction привязывается не к тому
# узлу: образ создаётся, но проверка размера молча не запускается.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_ota_headroom)
