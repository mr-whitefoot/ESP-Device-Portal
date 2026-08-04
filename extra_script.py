from datetime import date

Import("env")

# Имя устройства и версия приходят из окружения: одна кодовая база собирает
# несколько устройств, и по имени bin-файла должно быть видно, какое именно.
env.Replace(PROGNAME="ESP_%s_v%s" % (
    env.GetProjectOption("device_name"),
    env.GetProjectOption("release_version"),
))

# Дата сборки.
env.Append(CPPDEFINES=[("RELEASE_DATE", date.today().strftime("%d.%m.%Y"))])
