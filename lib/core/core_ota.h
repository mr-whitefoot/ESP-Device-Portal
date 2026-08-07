#pragma once

// Приём образа по HTTP. Свой, вместо CustomOTA из GyverPortal.
//
// Причина арифметическая. CustomOTA объявляет `Update.begin()` не размер
// присланного файла, а `getFreeSketchSpace() - 0x1000`. Область стейджинга
// встаёт на сектор дальше, чем нужно, и предельный образ становится на 4 КБ
// меньше. Для однометровых плат это разница между "обновилось" и "не влезло":
// релиз 4.3.0 не встал на реле, промахнувшись мимо предела на 48 байт, хотя
// espota тот же образ принимает -- она объявляет настоящий размер.
//
// Настоящий размер приходит параметром `?size=` от скрипта страницы обновления;
// он же приносит `?md5=` из манифеста, и тогда Update сверяет принятые байты.
// Это единственная сквозная проверка образа на пути зеркало -> браузер ->
// устройство: посчитать хэш в браузере нельзя, портал работает по http, а
// `crypto.subtle` доступен только в защищённом контексте.
//
// Ручная заливка файлом ни того, ни другого передать не может и работает по
// прежнему правилу CustomOTA -- с прежним пределом и без проверки хэша.
//
// Параметры приходят из URL, а не из формы, и это существенно: разбор
// multipart зовёт обработчик загрузки по ходу приёма тела, когда аргументы
// формы ещё не собраны. Аргументы строки запроса к этому моменту уже разобраны
// (Parsing-impl.h:180 против :192), поэтому размер известен в тот момент, когда
// он нужен -- на UPLOAD_FILE_START.
//
// Отказ от CustomOTA заодно освободил 5856 байт: там своя страница, свой JS и
// обновление файловой системы, которыми эта прошивка не пользуется.

namespace coreota {

// Учётные данные портала. Копия, а не указатель: строка обязана пережить запрос.
String user, pass;

// Итог последней попытки для страницы ответа. `failed` отдельно от текста:
// пустой текст бывает и у неудачи, если её причину не удалось назвать.
bool failed = false;
String failure;

// Образ дошёл до конца и принят. Отдельно от `failed`, потому что запрос может
// не содержать файла вовсе: тогда обработчик загрузки не позовут ни разу, и по
// одному лишь отсутствию ошибки пустой POST выглядел бы удачным обновлением --
// со страницей "success" и перезагрузкой.
bool received = false;


bool allowed(){
  if (!user.length()) return true;
  if (portal.server.authenticate(user.c_str(), pass.c_str())) return true;
  portal.server.requestAuthentication();
  return false;
}


void fail(const String& reason){
  failed = true;
  failure = reason;
  LOG_E(ota, String(F("web update failed: ")) + reason);
}


// Сколько места просить у Update. Ноль, мусор и отсутствие параметра дают
// прежнее правило CustomOTA. Верхнюю границу проверять не нужно: слишком
// большой размер Update.begin() отвергнет сам, вернув UPDATE_ERROR_SPACE, а
// слишком маленький оборвёт запись на превышении -- перелиться за объявленное
// он не даёт.
uint32_t requestedSize(){
  uint32_t asked = portal.server.arg(F("size")).toInt();
  if (asked) return asked;
  return (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
}


void handleUpload(){
  HTTPUpload& upload = portal.server.upload();

  if (upload.status == UPLOAD_FILE_START){
    failed = false;
    received = false;
    failure = "";
    if (!allowed()) { failed = true; return; }
    if (upload.name != F("firmware")) return fail(F("unexpected form field"));
    // Сжатый образ здесь не принимается намеренно. eboot распаковывает от нуля
    // вверх, не проверяя, не догнал ли он собственный источник, а промежуток
    // до области стейджинга у однометровых плат сейчас тоньше самого образа.
    if (!upload.filename.endsWith(F(".bin"))) return fail(F("not a .bin"));

    uint32_t size = requestedSize();
    String md5 = portal.server.arg(F("md5"));

    // Радио и OTA-порт не должны трогать флеш, пока идёт запись.
    WiFiUDP::stopAll();

    LOG_W(ota, String(F("web update started size=")) + size +
               F(" md5=") + (md5.length() ? "yes" : "no"));

    if (!Update.begin(size, U_FLASH)) return fail(Update.getErrorString());
    // Длину проверяет сам setMD5: чужая строка не должна становиться хэшем.
    if (md5.length()) Update.setMD5(md5.c_str());
    return;
  }

  // Уже сорвалось: остаток тела дочитывается, но никуда не пишется.
  if (failed) return;

  if (upload.status == UPLOAD_FILE_WRITE){
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      fail(Update.getErrorString());

  } else if (upload.status == UPLOAD_FILE_END){
    if (Update.end(true)) received = true;
    else fail(Update.getErrorString());

  } else if (upload.status == UPLOAD_FILE_ABORTED){
    Update.end();
    fail(F("aborted"));
  }
}


// Ответ на POST. Страницу собираем руками, а не билдером портала: билдер
// работает только внутри своего обхода, а сюда мы попадаем из обработчика
// сервера. Заодно ответ не зависит от того, в каком состоянии портал.
void handleFinish(){
  if (!allowed()) return;

  if (failed || !received){
    if (!failed) fail(F("no firmware file in request"));
    portal.server.send(200, F("text/html"),
      String(F("<meta charset='utf-8'><body style='background:#1b1b1b;color:#eee;"
               "font-family:sans-serif;text-align:center;padding-top:3em'>"
               "<h3>Update error</h3><p>")) + failure +
             F("</p><a style='color:#8cf' href='/ota_update'>Back</a>"));
    return;
  }

  // Успех: перезагрузка через общий заказ, а не здесь. Ответ уходит внутри
  // текущего прохода, а перезагрузка случится из loop() секундой позже.
  portal.server.send(200, F("text/html"),
    F("<meta charset='utf-8'><meta http-equiv='refresh' content='25; url=/'>"
      "<body style='background:#1b1b1b;color:#eee;font-family:sans-serif;"
      "text-align:center;padding-top:3em'>"
      "<h3>Update success</h3><p>Rebooting...</p>"));
  restartRequest("firmware updated");
}


// Маршруты регистрируются после portal.start(): сервер создаёт он. GET
// страницы обновления здесь не нужен -- этот адрес разбирает обычный обход
// портала в portalBuild().
void routes(const String& authUser, const String& authPass){
  user = authUser;
  pass = authPass;
  portal.server.on(F("/ota_update"), HTTP_POST, handleFinish, handleUpload);
}

}
