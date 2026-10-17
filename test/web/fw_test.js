// Стенд для web/fw.js: node test/web/fw_test.js
//
// Скрипт страницы обновления с 4.7.0 живёт на зеркале, а не во флеше, и потому
// не проходит ни через компилятор, ни через проверку размера образа. Между
// нажатием Install и записью во флеш не осталось ничего, кроме него, --
// отсюда этот стенд.
//
// Браузера здесь нет: DOM, XMLHttpRequest и FormData подменены ровно настолько,
// насколько ими пользуется скрипт. Проверяется то, что нельзя увидеть глазами
// на странице: какой файл и с каким объявленным размером уходит в POST, и обе
// границы безопасности, при нарушении которых заливаться нельзя вовсе.

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = fs.readFileSync(
  path.join(__dirname, '..', '..', 'web', 'fw.js'), 'utf8');
const BASE = 'https://mr-whitefoot.github.io/ESP-Device-Portal/';

// Настоящие размеры образов ESP-07S из релизов. Точные числа важны: почти все
// проверки ниже -- про арифметику, а не про строки.
const IMG = {
  '4.7.0': { raw: 477712, gz: 330446 },
  '4.6.0': { raw: 479232, gz: 331114 },
  '4.4.1': { raw: 479280, gz: 331034 },
};

function images(version, compressed) {
  const name = `ESP_Relay_ESP07S_8ch_v${version}.bin`;
  const size = IMG[version];
  const entry = compressed
    ? { file: name + '.gz', size: size.gz, md5: 'gz' + version,
        raw: size.raw, raw_file: name, raw_md5: 'raw' + version }
    : { file: name, size: size.raw, md5: 'raw' + version };
  return { Relay_ESP07S_8ch: entry };
}

function manifest(compressed) {
  const order = ['4.7.0', '4.6.0', '4.4.1'];
  const history = order.map(v => ({
    version: v, notes: `заметки ${v}`, images: images(v, compressed) }));
  // Верхние version/images дублируют свежую запись ради прошивок 4.3.1--4.6.0:
  // их скрипт вшит во флеш и про history не знает.
  return JSON.stringify(
    { version: '4.7.0', images: history[0].images, history });
}

const PHASE1 = manifest(false);
const PHASE2 = manifest(true);
// Манифест до 4.7.0: только свежий релиз, никакой истории.
const LEGACY = JSON.stringify(
  { version: '4.7.0', images: images('4.7.0', false) });

// ESP-07S с прошивкой 4.6.0: область скетчей 962560, занято 479232.
const ESP07S = { v: '4.6.0', i: 'Relay_ESP07S_8ch', f: 962560 - 479232,
                 e: 962560 };

function element() {
  return {
    textContent: '', style: {}, options: [], children: [],
    appendChild(child) { this.children.push(child); },
    get text() { return this.textContent || this.children.map(c => c.text).join(''); }, value: '', disabled: false,
    add(option) {
      this.options.push(option);
      if (this.options.length == 1) this.value = option.value;
    },
  };
}

// page: 'update' -- полный блок; 'main' -- только подсказка на главной.
function run(page, manifestText, FW, offline) {
  const nodes = {};
  (page == 'update' ? ['fwStatus', 'fwGo', 'fwVer', 'fwWhat'] : ['fwNew'])
    .forEach(id => nodes[id] = element());
  // Прошивка отдаёт выбор версии и кнопку уже скрытыми: style='display:none'.
  if (nodes.fwGo) nodes.fwGo.style.display = 'none';
  if (nodes.fwVer) nodes.fwVer.style.display = 'none';

  const posts = [];

  function XHR() { this.upload = {}; }
  XHR.prototype.open = function (method, url) {
    this.method = method;
    this.url = url;
  };
  XHR.prototype.send = function (body) {
    if (this.method == 'POST') {
      posts.push({ url: this.url, file: body.parts[0][1] });
      this.onload();
      return;
    }
    if (offline) { this.onerror(); return; }
    this.status = 200;
    if (this.url.endsWith('manifest.json')) this.responseText = manifestText;
    else this.response = { downloaded: this.url };
    this.onload();
  };

  const sandbox = {
    window: {}, FW, XMLHttpRequest: XHR, setTimeout() {},
    FormData: function () {
      this.parts = [];
      this.append = (field, value, name) => this.parts.push([field, name]);
    },
    Option: function (text, value) { return { text, value }; },
    document: {
      currentScript: { src: BASE + 'fw.js' },
      getElementById: id => nodes[id] || null,
      createElement: () => element(),
    },
  };
  sandbox.window.FW = FW;
  vm.createContext(sandbox);
  vm.runInContext(SRC, sandbox);

  return {
    nodes, posts,
    install: () => sandbox.window.fwInstall(),
    choose(version) {
      nodes.fwVer.value = version;
      nodes.fwVer.onchange();
    },
  };
}

let failed = 0;

function check(name, got, want) {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) {
    console.log('  ok   ' + name);
  } else {
    failed++;
    console.log(' FAIL  ' + name);
    console.log('        получено  ' + JSON.stringify(got));
    console.log('        ожидалось ' + JSON.stringify(want));
  }
}

function group(name) { console.log('\n— ' + name); }

group('страница обновления, манифест фазы 1 (несжатый)');
let r = run('update', PHASE1, ESP07S);
check('статус', r.nodes.fwStatus.textContent, 'New version 4.7.0 available');
check('версии в списке', r.nodes.fwVer.options.map(o => o.value),
      ['4.7.0', '4.6.0', '4.4.1']);
check('своя версия помечена', r.nodes.fwVer.options[1].text,
      '4.6.0 (installed)');
check('что нового', r.nodes.fwWhat.textContent, 'заметки 4.7.0');
check('список показан', r.nodes.fwVer.style.display, '');
check('кнопка показана', r.nodes.fwGo.style.display, '');
r.install();
check('объявлен размер несжатого образа', r.posts[0].url,
      '/ota_update?size=477712&md5=raw4.7.0');
check('уходит .bin', r.posts[0].file, 'ESP_Relay_ESP07S_8ch_v4.7.0.bin');

group('страница обновления, манифест фазы 2 (сжатый)');
r = run('update', PHASE2, ESP07S);
r.install();
// Объявляется размер .gz -- ровно то, что передаётся и стейджится.
check('объявлен размер сжатого образа', r.posts[0].url,
      '/ota_update?size=330446&md5=gz4.7.0');
check('уходит .bin.gz', r.posts[0].file,
      'ESP_Relay_ESP07S_8ch_v4.7.0.bin.gz');

group('откат на выбранную версию');
r = run('update', PHASE2, ESP07S);
r.choose('4.4.1');
check('что нового переключилось', r.nodes.fwWhat.textContent, 'заметки 4.4.1');
r.install();
check('уходит образ 4.4.1', r.posts[0].file,
      'ESP_Relay_ESP07S_8ch_v4.4.1.bin.gz');
check('выбор заблокирован на время заливки', r.nodes.fwVer.disabled, true);
check('кнопка спрятана на время заливки', r.nodes.fwGo.style.display, 'none');

group('первая граница: стейджинг не должен залезть на работающую прошивку');
// 479280 округляется до 483328, свободного места на байт меньше.
r = run('update', PHASE1, { ...ESP07S, f: 483327 });
r.choose('4.4.1');
r.install();
check('отказ', r.nodes.fwStatus.textContent, 'No room: 483328+undefined');
check('ничего не отправлено', r.posts.length, 0);
// Ровно столько же места -- уже помещается.
r = run('update', PHASE1, { ...ESP07S, f: 483328 });
r.choose('4.4.1');
r.install();
check('впритык помещается', r.posts[0].file,
      'ESP_Relay_ESP07S_8ch_v4.4.1.bin');

group('вторая граница: распаковка не должна догнать собственный источник');
// Источник лежит с адреса e − round(gz); распакованные raw байт обязаны
// кончиться раньше. Здесь конец области занижен искусственно.
r = run('update', PHASE2, { ...ESP07S, f: 962560, e: 400000 });
r.install();
check('отказ', r.nodes.fwStatus.textContent, 'No room: 331776+477712');
check('ничего не отправлено', r.posts.length, 0);
// Для несжатой записи поля raw нет, и проверка выключается сама.
r = run('update', PHASE1, { ...ESP07S, f: 962560, e: 400000 });
r.install();
check('несжатую запись вторая граница не трогает', r.posts.length, 1);

group('манифест до 4.7.0, без истории');
r = run('update', LEGACY, ESP07S);
check('одна версия в списке', r.nodes.fwVer.options.map(o => o.value),
      ['4.7.0']);
check('что нового пустое', r.nodes.fwWhat.textContent, '');
r.install();
check('установка работает', r.posts[0].file,
      'ESP_Relay_ESP07S_8ch_v4.7.0.bin');

group('в релизе нет образа для этого устройства');
r = run('update', PHASE2, { ...ESP07S, i: 'Relay_ESP32' });
r.install();
check('сообщение', r.nodes.fwStatus.textContent, 'No image for Relay_ESP32');
check('ничего не отправлено', r.posts.length, 0);

group('подсказка на главной');
r = run('main', PHASE2, ESP07S);
check('о новой версии сообщает', r.nodes.fwNew.text,
      'New version 4.7.0 available');
check('подсказка -- ссылка на страницу обновления',
      r.nodes.fwNew.children[0].children[0].href, '/ota_update');
r = run('main', PHASE2, { ...ESP07S, v: '4.7.0' });
check('об актуальной молчит', r.nodes.fwNew.text, '');
r = run('main', PHASE2, ESP07S, true);
check('о недоступном GitHub молчит', r.nodes.fwNew.text, '');

group('сравнение версий не строковое');
r = run('main', JSON.stringify({ version: '4.10.0', images: {} }),
        { ...ESP07S, v: '4.9.0' });
check('4.10.0 новее 4.9.0', r.nodes.fwNew.text,
      'New version 4.10.0 available');

group('GitHub недоступен, страница обновления');
r = run('update', PHASE2, ESP07S, true);
check('статус', r.nodes.fwStatus.textContent, 'GitHub unreachable');
check('кнопка осталась скрытой', r.nodes.fwGo.style.display, 'none');
check('выбор версии остался скрытым', r.nodes.fwVer.style.display, 'none');

group('свежая версия уже стоит');
r = run('update', PHASE2, { ...ESP07S, v: '4.7.0' });
check('статус', r.nodes.fwStatus.textContent, 'Up to date');
check('кнопка всё равно доступна', r.nodes.fwGo.style.display, '');
r.install();
check('переустановка той же версии', r.posts[0].file,
      'ESP_Relay_ESP07S_8ch_v4.7.0.bin.gz');

console.log(failed
  ? `\n${failed} проверок провалено`
  : '\nвсе проверки пройдены');
process.exit(failed ? 1 : 0);
