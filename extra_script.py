from datetime import date

Import("env")

# Имя устройства и версия приходят из окружения: одна кодовая база собирает
# несколько устройств, и по имени bin-файла должно быть видно, какое именно.
env.Replace(PROGNAME="ESP_%s_v%s" % (
    env.GetProjectOption("custom_device_name"),
    env.GetProjectOption("release_version"),
))

# Дата сборки.
env.Append(CPPDEFINES=[("RELEASE_DATE", date.today().strftime("%d.%m.%Y"))])

# Имя образа для страницы обновления: по нему прошивка находит в manifest.json
# свой файл среди пяти. Берётся оттуда же, откуда имя bin-файла, поэтому
# разъехаться с релизом не может.
env.Append(CPPDEFINES=[("IMAGE_NAME", env.GetProjectOption("custom_device_name"))])
