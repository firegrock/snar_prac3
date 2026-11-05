# Регистратор: Ubuntu 20.04 / macOS

Python **3.8+**. Встроенные `argparse/csv/json/sqlite3`, pyserial **3.5**,
websocket-client **1.8.0** (обе версии поддерживают 3.8). Не менять системный Python,
не делать `sudo pip`. Сетевые зависимости загрузить до подключения к учебной AP.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip==24.3.1
python -m pip install -e host
python -m snar_imu --help
python -m snar_imu ports
```

Если Ubuntu не имеет venv/доступа к serial, администратор учебного образа заранее
устанавливает пакет `python3-venv` и права группы `dialout`; CLI не требует root.
Pip24.3.1 и setuptools70.3.0 (build-system проекта) совместимы с Python3.8.
Обновление pip происходит **только внутри venv**: штатный pip20 Ubuntu20.04
ещё не поддерживает editable-install PEP660. Системный pip не изменяется.
На macOS обычно `/dev/cu.usbserial-*`, не Bluetooth/debug-console. Не выбирать
первый случайный порт. Все соединения требуют явного `--device-id`; опциональный
`--uid` дополнительно закрепляет ранее известную плату. UID закрепляется и при
первом handshake текущего процесса, не только IP.

```sh
python -m snar_imu menu --serial /dev/ttyUSB0 --device-id 1
python -m snar_imu status --ws ws://192.168.4.1/ws --device-id 1
python -m snar_imu record --serial /dev/ttyUSB0 --device-id 1 --output runs/static_01
python -m snar_imu record --ws ws://192.168.4.1/ws --device-id 1 --output runs/comau_01
python -m snar_imu validate runs/static_01
python -m snar_imu compare runs/usb_01 runs/wifi_01
```

`--duration 600` — ограниченная десятиминутная запись; иначе Ctrl+C.
`--interactive`: `m текст` — метка, `q` — выход. `--observe` присоединяется без
начала/завершения опыта другого клиента. Каждая запись — новый каталог; существующий
не перезаписывается. При новом boot/stream создаётся `..._segment_002` и сообщение;
времена разных boot не склеиваются. Смена UID — ошибка, а не новый разрешённый модуль.

Без меню доступны все протокольные команды:

```sh
python -m snar_imu command --serial /dev/ttyUSB0 --device-id 1 set_config --params '{"config":{"Kp":2,"Ki":0,"sample_rate_hz":100}}'
python -m snar_imu command --serial /dev/ttyUSB0 --device-id 1 lcd --params '{"page":4,"backlight":true}'
python -m snar_imu export-cal --serial /dev/ttyUSB0 --device-id 1 cal_active.json
python -m snar_imu import-cal --serial /dev/ttyUSB0 --device-id 1 cal_new.json
```

Импорт только создаёт кандидата. Затем отдельно `command ... cal_apply` и
`command ... cal_save`. Перенос между UID требует `--confirm-transfer`.
`calibrate --kind gyro|six --output ...` одновременно сохраняет raw мастера и
итоговые candidate.json/candidate_numeric.csv; во время six вводятся названия граней.

Octave пишет десять коэффициентов CSV, без JSON-пакета. Сохраните базовый JSON
экспортом, затем объедините идентичность/единицы и числовые коэффициенты:

```sh
python -m snar_imu cal-from-csv cal_octave.csv --base cal_active.json --output cal_new.json
```

Числа проверяются и на ПК, и устройством. Базовые статистики остаются происхождением
базового JSON; метод меняется на `OCTAVE_AXIS_MODEL`. Статистику собственных исходных
записей следует явно добавить к отчёту, не представлять её за встроенный мастер.

## Поведение при сбоях

COBS/CRC-мусор и ROM-вывод пропускаются; валидная идентичность запрашивается снова.
DTR/RTS выставляются inactive **до** open, reset специально не выполняется; некоторые
драйверы всё же дают импульс — это обнаруживается по boot_id. Во время reconnect
не отправляются изменяющие команды до проверки номера/UID.

После flush CSV/индекса и transport-файла отправляется ACK. Это не транзакционная
гарантия от внезапного отключения питания ПК. Индекс SQLite — на диске, сортировка
и исключение дубликатов не требуют хранения всего опыта в RAM. При штатном завершении
samples.csv пересобирается упорядоченно из индекса. Питание ПК внезапно пропало —
сохраните исходный каталог для разбора; автоматическое восстановление прерванного
процесса в тот же каталог не заявлено. Сетевой reconnect внутри процесса поддержан.
