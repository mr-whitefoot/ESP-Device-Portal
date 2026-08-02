import os
import sys
from datetime import date

Import("env")

env.Replace(PROGNAME="ESP_Relay_v%s" % env.GetProjectOption("release_version"))

# Дата сборки. Раньше лежала литералом в main.cpp и правилась руками, то есть
# показывала в портале что угодно, кроме реальной даты прошивки.
env.Append(CPPDEFINES=[("RELEASE_DATE", date.today().strftime("%d.%m.%Y"))])


# Предел размера прошивки по возможности обновления по воздуху.
#
# При eagle.flash.1m64.ld под скетч отведено 962560 байт (_FS_start 0x402EB000),
# и именно это число показывает сборка. Но OTA укладывает новый образ рядом со
# старым, а не поверх: Updater требует, чтобы оба помещались одновременно.
# Значит устойчивый предел -- половина. Прошивка крупнее зальётся и заработает,
# но следующую по воздуху принять уже не сможет, а у ESP-01 нет USB, то есть
# восстановить её без пайки не выйдет.
#
# Проверяется размер бинарника, а не занятый флеш из отчёта сборки:
# по воздуху передаётся именно он.
SKETCH_AREA = 962560
OTA_MAX_SIZE = SKETCH_AREA // 2


def check_ota_headroom(source, target, env):
    # Именно с диска: SCons отдаёт для .bin закешированный размер, а файл
    # создаётся сторонним билдером elf2bin.
    size = os.path.getsize(target[0].get_abspath())
    left = OTA_MAX_SIZE - size

    if size > OTA_MAX_SIZE:
        print(
            "\nОшибка: прошивка %d байт, предел для OTA %d байт (превышение %d).\n"
            "Такую прошивку ещё можно залить по проводу, но обновить по воздуху\n"
            "уже нельзя: новый образ не поместится рядом со старым.\n"
            % (size, OTA_MAX_SIZE, -left)
        )
        sys.exit(1)

    print("OTA headroom: %d of %d bytes used (%.1f%%), %d left"
          % (size, OTA_MAX_SIZE, 100.0 * size / OTA_MAX_SIZE, left))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_ota_headroom)
