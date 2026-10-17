// Скрипт страницы обновления портала.
//
// Живёт на зеркале GitHub Pages, а не во флеше устройства. Причина
// арифметическая: у однометровых плат запас под OTA-образ измеряется сотнями
// байт, и полтора килобайта JS в PROGMEM -- это заметная доля бюджета.
// Следствие важнее экономии: страницу обновления теперь можно менять, не
// перепрошивая парк, и форма манифеста перестала быть замороженной в каждой
// выпущенной прошивке.
//
// Устройство сообщает о себе через window.FW (см. coreupdate::facts):
//   v -- своя версия, i -- имя своего образа в релизе,
//   f -- ESP.getFreeSketchSpace(), e -- конец области скетчей.
// Отсюда обе границы безопасности, которые считаются ниже.
//
// Элементы, которые скрипт ищет на странице (любого может не быть):
//   fwStatus -- строка состояния на странице обновления
//   fwNew    -- подсказка на главной, заполняется только при новой версии
//   fwGo     -- кнопка Install
//   fwVer    -- выбор версии
//   fwWhat   -- «Что нового» для выбранной версии

(function () {
  if (!window.FW) return;

  // Адрес зеркала берётся из собственного src, а не дублируется константой:
  // единственное место, где он записан, -- тег script в прошивке.
  var BASE = document.currentScript.src.replace(/[^/]*$/, '');

  var statusLine = document.getElementById('fwStatus');
  var hint = document.getElementById('fwNew');
  var goButton = document.getElementById('fwGo');
  var picker = document.getElementById('fwVer');
  var notes = document.getElementById('fwWhat');

  var releases = null;  // история релизов, свежий первым
  var chosen = null;    // запись выбранного релиза

  // news=true пишет ещё и в подсказку на главной. Недоступный GitHub и
  // актуальная прошивка новостями не считаются: главная открыта чаще всех
  // остальных страниц, и молчать ей уместнее.
  function say(text, news) {
    if (statusLine) statusLine.textContent = text;
    if (news && hint) {
      // Строка выглядит как остальные на главной (inliner > label) и ведёт на
      // страницу обновления. Размер шрифта и выравнивание даёт CSS портала.
      var link = document.createElement('a');
      link.href = '/ota_update';
      link.textContent = text;
      link.style.color = 'inherit';
      var label = document.createElement('label');
      label.appendChild(link);
      hint.textContent = '';
      hint.className = 'inliner';
      hint.style.width = '100%';
      hint.appendChild(label);
    }
  }

  function fetch(url, ok, bad, asBlob) {
    var request = new XMLHttpRequest();
    request.open('GET', url);
    if (asBlob) {
      request.responseType = 'blob';
      request.onprogress = function (event) {
        say('Downloading ' + ((100 * event.loaded / (event.total || 1)) | 0) + '%');
      };
    }
    request.onload = function () {
      request.status == 200 ? ok(request) : bad();
    };
    request.onerror = bad;
    request.send();
  }

  // Сравнение трёх чисел версии. Строковое сравнение здесь неверно: '4.10.0'
  // меньше '4.9.0' лексикографически и больше по существу.
  function newer(a, b) {
    a = a.split('.');
    b = b.split('.');
    for (var i = 0; i < 3; i++) {
      var d = (+a[i] || 0) - (+b[i] || 0);
      if (d) return d > 0;
    }
    return false;
  }

  function select() {
    var version = picker ? picker.value : releases[0].version;
    for (var i = 0; i < releases.length; i++) {
      if (releases[i].version == version) chosen = releases[i];
    }
    if (notes) notes.textContent = (chosen && chosen.notes) || '';
  }

  fetch(BASE + 'manifest.json', function (response) {
    var manifest = JSON.parse(response.responseText);

    // Манифест до 4.7.0 описывал только свежий релиз. Приводим к общей форме,
    // чтобы дальше существовал ровно один путь.
    releases = manifest.history ||
      [{ version: manifest.version, images: manifest.images }];

    if (picker) {
      for (var i = 0; i < releases.length; i++) {
        var version = releases[i].version;
        picker.add(new Option(
          version == FW.v ? version + ' (installed)' : version, version));
      }
      picker.onchange = select;
      picker.style.display = '';
    }
    select();

    // Кнопка показывается независимо от сравнения версий: выбор версии
    // существует и ради отката, и ради повторной установки той же самой.
    if (goButton) goButton.style.display = '';

    var fresh = newer(manifest.version, FW.v);
    say(fresh ? 'New version ' + manifest.version + ' available' : 'Up to date',
        fresh);
  }, function () {
    say('GitHub unreachable');
  });

  window.fwInstall = function () {
    var image = chosen && chosen.images[FW.i];
    if (!image) {
      say('No image for ' + FW.i);
      return;
    }

    // Две границы безопасности. Первая -- та же, с которой сверяется
    // Update.begin(): область стейджинга встаёт на round(size) ниже конца
    // области скетчей и не должна залезть на работающую прошивку.
    //
    // Вторая нужна сжатому образу: eboot распаковывает от нуля вверх, не
    // проверяя, не догнал ли он собственный источник. Источник лежит с адреса
    // (конец области − round(size)), и распакованный образ обязан кончиться
    // раньше. Для несжатой записи raw в манифесте нет, сравнение с undefined
    // ложно, и проверка сама собой выключается.
    var staged = (image.size + 4095) & -4096;
    if (staged > FW.f || image.raw > FW.e - staged) {
      say('No room: ' + staged + '+' + image.raw);
      return;
    }

    if (goButton) goButton.style.display = 'none';
    if (picker) picker.disabled = true;

    fetch(BASE + image.file, function (response) {
      var form = new FormData();
      form.append('firmware', response.response, image.file);

      // Настоящий размер и md5 уходят в строке запроса, а не в форме: разбор
      // multipart зовёт обработчик загрузки по ходу приёма тела, когда поля
      // формы ещё не собраны, а аргументы строки запроса уже разобраны.
      var upload = new XMLHttpRequest();
      upload.open('POST', '/ota_update?size=' + image.size + '&md5=' + image.md5);
      upload.upload.onprogress = function (event) {
        say('Flashing ' + ((100 * event.loaded / event.total) | 0) + '%');
      };
      upload.onload = function () {
        say('Done, rebooting');
        setTimeout(function () { location.href = '/'; }, 20000);
      };
      upload.onerror = function () { say('Upload failed'); };
      upload.send(form);
    }, function () { say('Download failed'); }, 1);
  };
})();
