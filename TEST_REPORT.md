# Фактический отчёт испытаний

Дата: 2026-11-05. macOS, одна подключённая плата. Это отчёт о выполненных
проверках, не сертификат электрической/механической безопасности.

## Окружение и резервная копия

| Проверка | Статус | Основание |
|---|---|---|
| Каталог и сохранность чужих проектов | PASS | Исходно пустой SNAR_prac3; проект создан здесь, соседние проекты не изменялись |
| SDK | PASS | ESP-IDF **v6.1-dev-8798-g4d80c1b29ae**, commit `4d80c1b29aeabc8e735ffc6e1be9bb23e95d1d5d`; registry6.2.0, без обновления |
| Плата/flash | PASS | esptool5.6.0: ESP32-D0WD-V3 rev3.1, baseMAC2c:bc:bb:0d:72:50, flash4MB |
| Backup460800 | FAIL | Corrupt data; не объявлен успешным backup |
| Полный backup115200 | PASS | 4194304 байта, SHA256 `a788f570240b6bc1e5753bec40feed9af606cb7cf34557da57a4098cc4dcde07`, `backups/` |
| Сборки01–04 | PASS | `build-01.log`…`build-04.log`, `builds-verified.log`: реальные compile definitions, WS/socket config, image fits |
| Изоляция окружения | PASS | Python venv; SDK/системный Python/сеть хоста не переустанавливались; push/публикации/eFuse/erase-flash/Comau не выполнялись |

## Программные

`sh scripts/test.sh`, `tests-software.log`; native ASan/UBSan — `tests-sanitized.log`.

| Группа | Статус | Что реально проверено |
|---|---|---|
| Raw/SI/оси | PASS | int16 sign, диапазоны/масштабы, rad/s, proper R_BS |
| Калибровка | PASS | известные bias/scale, six solve, неверная грань/движение/NaN/scale/UID |
| Mahony | PASS | покой, четыре известных roll/pitch, yaw без абсолютной коррекции, знак feedback, q/-q, norm, zero a, dt gap, Ki |
| C/Python/JavaScript codec | PASS | общий vector, ISO-HDLC check, COBS, endian, 64бит/BigInt, corruption/length/boot noise/lost delimiter; JS исполнен JavaScriptCore |
| Ring/cursors | PASS | четыре native consumer threads, один producer и caller lock; overwrite/GAP/resume/seq limit |
| Policy/cache/NVS validator | PASS | production C: запрет мутаций в опыте, idempotency cache, version/size/CRC/UID |
| Регистратор/files | PASS | disk dedup/order, gaps/full keys, compare, identity pin; реальный CLI+PTY: corrupt frame/resume/Ctrl+C |
| TCP reconnect/регрессии | PASS | реальный websocket-client против локального SYNTHETIC RFC6455 peer; reconnect, resume без повторного open, samples внутри RPC без ложного timeout; не ESP HTTPD/радио |
| Python3.8 | NOT RUN | грамматика проверена PASS; реальное исполнение на3.10, интерпретатора3.8 нет |
| Ubuntu20.04 | NOT RUN | в доступном окружении macOS; установка/команды подготовлены |
| C/Octave fixture и Octave loader/plots | NOT RUN | Octave не установлен; исходники/fixture/test_fixture готовы; C/Python не выдаются за Octave |
| GUI браузера | NOT RUN | codec исполнен; визуальная проверка страницы на AP отдельно не выполнялась |

SYNTHETIC пример `examples/synthetic/static`: validate/replay C PASS,
600 искусственных записей, не аппаратные данные. Кодек JS не требует Node/npm.
Текущий suite: native C + **12 Python-тестов** + JavaScriptCore, все PASS
(`websocket-fix-tests.log`; более ранний прогон — `tests-software.log`);
CMake отдельно отвергает LAB_DEVICE_ID=5. Octave явно остаётся NOT RUN.

## Аппаратные наблюдения и исправления

- Подтверждены UART HELLO/commands, UID **49189103694417** (SoftAP MAC), версии,
  metadata и сохраняемый snapshot. Мост реально меняет boot_id при open несмотря
  на inactive DTR/RTS: добавлено ожидание HELLO; намеренный reset в регистраторе отсутствует.
- Промежуточная процедура FIFO при sleep: **FAIL**, FIFO_EN читался0 вместоF8.
  Исправлена: явно отброшенный reference DRDY, включение FIFO в проверенном
  полупериодном окне без sleep. Readback и реальные samples затем **PASS**;
  `hardware-alignment-status.log`: WHO_AM_I0x68, ориентация ready, counters0.
  Старые FAIL-логи оставлены для аудита, не подменены синтетикой.
- Первая попытка10мин: **FAIL как непрерывное испытание**. Ложный host timeout,
  затем reopen/reset и два честно раздельных boot-сегмента. MCU sensor counters
  до обрыва0, сенсор продолжал формировать записи. Исправлено обновление активности
  также для samples, потреблённых внутри RPC; добавлен regression test.
  `hardware-100hz-10min.log`, `hardware-100hz-investigation.log`.
  Первый сегмент20399 samples, 99.84334Гц, structural errors0; production C replay
  PASS, max sign-invariant q error1.11e-6. Это **не PASS десятиминутной непрерывности**.

- Первый тест длинного resume после анализа лога: **FAIL**. Старый тест проверял
  только наличие GAP и пропускал бесконечное догоняние oldest. Его ошибочный PASS
  отозван в `runs/hardware_resume/report.json`, исходные записи сохранены.
  Исправление сохраняет bounded follow-up пачку вместе с GAP. Усиленный тест
  требует продвижения и возврата к live; результат ниже.

## Итоговые выполненные аппаратные проверки

| Проверка | Статус | Данные / лог |
|---|---|---|
| Загрузка образов01,02,03,04 и возврат01 | PASS | **Одна физическая плата**, `runs/hardware_images/`, `hardware-images.log`; номер/UID/SDK/заявленный SSID проверены по UART, не RF-сканированием |
| Data Ready/FIFO/readback и отсутствие LCD | PASS | Реальные raw и q, WHO_AM_I0x68, измерения при ACK0 на шине LCD; не проверка электрических уровней |
| 50/100/200Гц и история832 записи | PASS | `runs/hardware_rates/report.json`, данные ниже; короткий UART resume без потерь |
| Gyro master30с | PASS | 3000 отсчётов, mean/std/температура и raw: `runs/hardware_gyro/`; подтверждены инженерные критерии, не метрологическая неподвижность |
| Импорт/apply/save/NVS после reboot | PASS | Коэффициенты совпали, статус LOADED, отрицательный scale отклонён; `runs/hardware_gyro/report.json` |
| Независимая проверочная запись после калибровки | PASS | Отдельная загрузка и отдельный опыт, `runs/hardware_acceptance_100hz/`; не повтор калибровочных3000 отсчётов |
| Короткий replay: неизменность записей | PASS | `runs/hardware_resume_fixed/report.json`: 165 повторных записей с точно теми же raw/t_us/q/flags; дубликаты исключены из итогового CSV |
| Длинная пауза и фактическое продолжение | PASS | 200Гц, пауза5,3с: один GAP [359,612), ровно253 невосстановленных записи; остаток истории получен, lag до live5 записей; тот же boot/stream |
| request_id и восстановление UART после мусора | PASS | `hardware-delivery-checks.json`: reset выполнен один раз, stream не изменился при повторе, битый COBS не помешал последующей команде |
| Окончательная сборка и прошивка01 | PASS | Все четыре build-каталога пересобраны; `flash-delivery.log`, затем15с диагностики/измерений без reboot, `hardware-delivery-checks.json` |

Последний длительный прогон после исправления GAP:

- **PASS**, `hardware-acceptance-100hz.log`, `hardware-acceptance-validation.log`.
- **59920** уникальных отсчётов; интервал между первым и последним **600,127783с**.
- Фактическая частота **99,843736Гц**, dt min/mean/max:
  **10009 / 10015,650845 / 10022мкс**. Не требуется идеальных10000мкс.
- Потерь seq, временных разрывов, ORIENTATION_INVALID, sensor-gap flags,
  структурных ошибок и дубликатов — **0**. FIFO/IRQ/sync/read/resync counters —0;
  один начальный DRDY сознательно отброшен при выравнивании до начала опыта.
- Один boot_id `17723503110915433335`; **746** снимков status; нет disconnect,
  повторной подписки, смены stream/config, reset/watchdog по доступной телеметрии.
- Free heap **44704…45404байт**, первый/последний снимок45404/45388;
  minimum free heap **36804байта**, нарастающей утечки в этом прогоне не выявлено.
  Стек high-water bytes: sensor4276, LCD1832, transport4424, commands4144.
- `frame_errors=138` включает отвергнутый ROM/boot-мусор до HELLO. Раздельный
  счётчик ошибок до HELLO в этом прогоне не записывался; эти138 не объявляются
  потерянными измерениями. Дыр/DELIVERY_HOLE в научной записи нет.
- Production C replay по сохранённому snapshot: **PASS**, 59920 состояний,
  max `min(norm(q1-q2),norm(q1+q2))` = **1,2102e-7**, допуск2e-4.
  Это согласованность реализации/записи, **не** C/Octave и не точность датчика.
- При финальном упорядочении кода неизменённые численные критерии калибровки вынесены в именованные
  LAB_CAL_* константы; все сборки и тесты повторены, актуальная01 прошита и
  проверена15с. Длительный тест не подменяется этими15с.

Измеренные частоты отдельных режимов (`runs/hardware_rates/`):

| Номинал,Гц | Фактически,Гц | dt min…max,мкс | Невосстановленные записи |
|---:|---:|---:|---:|
| 50 | 49,921723 | 20027…20035 | 0 |
| 100 | 99,843464 | 10011…10020 | 0 |
| 200 | 199,687052 | 5003…5012 | 0 |

Ёмкость832 соответствует примерно16,66/8,33/4,17с при измеренных частотах;
проверены заполнение ring и выдаваемые диапазоны. Это не гарантия радиопаузы.
Raw/SI/исходное состояние при этих режимах также прошли production C replay.

На оставленной плате: **комплект01**, UID49189103694417,100Гц,±4g,±500°/с,
DLPF4,Kp2,Ki0. В NVS сохранена **только проверенная gyro-калибровка**;
акселерометрические bias0/scale1, физический six-face мастер не выполнялся.

## Последующее исправление WebSocket

На установленном SDK `httpd_uri()` завершает handshake и **не вызывает** на нём
URI-handler. Прежний код ждал этот вызов для регистрации endpoint, поэтому первый
JSON-кадр приводил к закрытию соединения. Это **FAIL прежнего обработчика**,
не ошибка MPU и не успешно пройденный ранее Wi-Fi-тест.

Исправлено в `main/transport.c`: endpoint регистрируется на первом кадре после
проверки `HTTPD_WS_CLIENT_WEBSOCKET`, без предположения `method==HTTP_GET`;
PONG корректно потребляется. Веб-интерфейс показывает close code/reason,
отменяет pending RPC при закрытии и игнорирует callbacks старого сокета.

- **PASS**: production C handler компилируется с mocks SDK и получает первый
  JSON без предшествующего handshake callback; проверены повторный кадр,
  лимит slots, PONG, bounds и BUSY (`tests/test_ws_handler.py`).
- **PASS**: настоящий JS интерфейса исполнен с DOM/WebSocket mocks в
  JavaScriptCore: handshake/UID, вывод MPU, закрытие и повторное подключение
  (`tests/web_connection_jsc.js`). Это не визуальный браузер и не RF.
- **PASS**: сборка/прошивка01 (`websocket-fix-build.log`, `websocket-fix-flash.log`),
  после прошивки по USB sensor_ok=true, orientation_ready=true, next_seq175
  (`websocket-fix-status.log`). Адрес MPU восстановлен на согласованный0x68.
- **NOT RUN**: сквозной запрос через реальную Wi-Fi-сеть с этого компьютера;
  `192.168.4.1` с его текущего интерфейса недоступен. Проверка браузером пользователя
  после обновления страницы остаётся отдельным этапом.

## Недоступные физические условия

| Проверка | Статус | Причина / процедура |
|---|---|---|
| Мультиметр: питание/подтяжки/3V3 и5V стороны | NOT RUN | Нужен человек; `docs/wiring.md`. Программа не проверяет электрическую безопасность |
| LCD модель/адрес/pin mapping/контраст/текст | NOT RUN | На I²C1 ACK0; дисплей не подтверждён. При этом MPU/UART работают |
| Шесть физических граней/известные аппаратные наклоны | NOT RUN | Нет человека для установки и независимого углового эталона |
| Настоящий Wi-Fi HTTPD/UI и USB+Wi-Fi compare | NOT RUN | Хост использует рабочую en0 сеть192.168.0.137; не переключался автоматически на учебную AP |
| Четыре WS на ESP32 | NOT RUN | Нужен доступ к её AP; native threads/локальный RFC6455 peer не заменяют ESP HTTPD |
| Четыре Wi-Fi станции / четыре физические платы | NOT RUN | Доступны одна плата и один компьютер; четыре образа одной платы не равны четырём платам |
| Radio reconnect, медленная RF станция | NOT RUN | UART/socket resume не считается переподключением радио |
| Временная привязка независимым анализатором | NOT RUN | Проверены IRQ/FIFO counters и распределение dt, но внешнего измерения задержки ISR/DRDY нет |
| Физическое отключение/неисправность MPU на последней сборке | NOT RUN | Нужен человек для переподключения при снятом питании; промежуточные ошибки register readback сохранены отдельно |
| Сверка именно Register Map rev4.2 | NOT RUN | URL производителя вернул страницу поиска; прочитанная копия rev4.0 не выдаётся за rev4.2, подробности в docs/sources.md |
| Comau и механическое крепление | NOT RUN | Преподаватель/правила лаборатории; ПО не запускало и не модифицировало манипулятор |

Воспроизводимые ручные процедуры: `docs/testing.md`. Глубина RAM не является
безусловной гарантией4сек радиопаузы. Полная аппаратная приёмка проекта пока
не завершена, даже если сборки и перечисленные программные тесты PASS.
