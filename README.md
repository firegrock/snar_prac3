# СНАР · ESP32 + MPU-6050

Готовый исходный проект лабораторного инерциального модуля: ESP-IDF/FreeRTOS,
SoftAP, USB + WebSocket, Mahony IMU, мастер/импорт калибровки, LCD1602,
Python-регистратор и базовая обработка GNU Octave. Без Arduino, облака и управления
Comau. Статус реальных проверок — **[TEST_REPORT.md](TEST_REPORT.md)**, а не факт
наличия исходного кода. Непроверенные условия не считаются выполненными испытаниями.

**Фактически проверено:** сборки 01–04, реальная USB-запись более 10 минут,
50/100/200 Гц, gyro-мастер/NVS, короткий resume и длинный GAP с продолжением.
**NOT RUN:** радио/четыре станции, LCD, физические шесть граней, запуск Octave,
электрическая и механическая приёмка. Подробности и исходные логи — в отчёте.

## Сначала безопасность

**До включения соберите и проверьте [схему](docs/wiring.md).** LCD питается от
проверенных USB 5 В через отдельное распределение питания; его SDA/SCL подключаются
к GPIO25/26 **только через двунаправленный BSS138/open-drain преобразователь**.
GPIO ESP32 не допускают прямого подключения к 5 В. MPU — отдельная шина GPIO21/22,
INT GPIO27, 3,3 В. Не соединяйте питание компьютера и пауэрбанка.

## Быстрый старт преподавателя

На этой macOS установлен SDK с Git-версией
`v6.1-dev-8798-g4d80c1b29ae` (внутренний номер 6.2.0).
Он **не переустанавливается**. Версия/пакеты закреплены в
`docs/build-flash-macos.md`, `dependencies.lock`, `host/requirements.txt`.

```sh
. "$HOME/esp/esp-idf/export.sh"
idf.py -B build-01 -DLAB_DEVICE_ID=1 build
# Выберите проверенный порт, закройте другие мониторы. Flash — с reset платы.
idf.py -B build-01 -p /dev/cu.usbserial-110 -b 115200 flash

python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip==24.3.1
python -m pip install -e host
python -m snar_imu ports
python -m snar_imu menu --serial /dev/cu.usbserial-110 --device-id 1
```

Для Ubuntu используйте фактический `/dev/ttyUSB0` и Python 3.8+.
Установите зависимости **до** подключения к учебной AP без Интернета.
Подробнее: [регистратор](docs/host-install.md), [сборка четырёх комплектов](docs/build-flash-macos.md).

Подключитесь к `SNAR_IMU_01`, WPA2-PSK `12345678`. Откройте
`http://192.168.4.1/`, введите ожидаемый номер и сверяйте UID. Номер задаётся
сборкой, **не NVS**. MAC/eFuse UID отличает платы с одинаковым номером.

## Калибровка и первый опыт

Оси S определите по микросхеме/документации конкретного модуля, а не по направлению
USB-разъёма. Уложите модуль неподвижно и дайте фильтру подтвердить покой.

```sh
# Сохраняются исходные raw мастера; результат ещё не применяется.
python -m snar_imu calibrate --ws ws://192.168.4.1/ws --device-id 1 --kind gyro --output runs/cal_gyro_01
python -m snar_imu command --ws ws://192.168.4.1/ws --device-id 1 cal_apply
python -m snar_imu calibrate --ws ws://192.168.4.1/ws --device-id 1 --kind six --output runs/cal_six_01
# Установите положение, затем вводите +X, -X, +Y, -Y, +Z, -Z; по 5 с.
python -m snar_imu command --ws ws://192.168.4.1/ws --device-id 1 cal_apply
python -m snar_imu command --ws ws://192.168.4.1/ws --device-id 1 cal_save

# Независимая проверочная запись, НЕ калибровочная.
python -m snar_imu record --serial /dev/cu.usbserial-110 --device-id 1 --output runs/static_01
python -m snar_imu validate runs/static_01
# На Comau: только Wi-Fi и пауэрбанк, после разрешения преподавателя.
python -m snar_imu record --ws ws://192.168.4.1/ws --device-id 1 --output runs/comau_01
```

Ctrl+C завершает файловую запись и явно завершает собственный опыт на устройстве,
если связь сохранилась. При неудаче завершения выдаётся предупреждение: опыт не
сбрасывается автоматически при отключении клиента. Для второго наблюдателя:
`record ... --observe --output runs/observer_01`. Веб-кнопка начала опыта **не
создаёт CSV**; полный поток сохраняет только регистратор ПК.

Для USB-калибровки используйте **одно подключение меню**: пункты 3/4 (мастер),
затем 7 (применить), 8 (NVS). Если USB-мост сбрасывает плату при каждом open,
отдельные CLI-процессы потеряют RAM-кандидата; Wi-Fi или непрерывное меню избегают
этой особенности. Внешний JSON при необходимости сохраните до переподключения.

```octave
addpath('octave'); addpath('octave/reference');
r = snar_load('runs/static_01');
snar_plot(r);
q_check = replay_run(r); % откажет в точном сравнении без исходной границы
```

Без оборудования: готовый опыт `examples/synthetic/static` помечен
**SYNTHETIC**, не является измерением. Новый пример:
`python scripts/synthetic.py runs/synthetic_new`.

## Проверки и документы

```sh
sh scripts/test.sh
python -m snar_imu compare runs/usb_01 runs/wifi_01
```

- [Провода и измерения мультиметром](docs/wiring.md)
- [Сборка/backup/flash](docs/build-flash-macos.md), [установка CLI](docs/host-install.md)
- [Протокол](docs/protocol.md), [файлы/Octave](docs/data-format.md)
- [Системы координат и Mahony](docs/coordinates.md), [калибровка](docs/calibration.md)
- [Wi-Fi и resume](docs/wifi-resume.md), [RAM и архитектура](docs/architecture.md)
- [Испытания](docs/testing.md), [неисправности](docs/troubleshooting.md)
- [Источники и границы утверждений](docs/sources.md), [сторонние лицензии](THIRD_PARTY_NOTICES.md)

RAM-ring — история минимум 4 с, **не гарантия перенести 4 с отсутствия радио**:
в окно входят обнаружение обрыва, reconnect, запрос и досылка. При reboot RAM
исчезает. Yaw относительный и дрейфует; нет магнитометра, ZUPT, скрытого подавления
динамики или расчёта навигационных координат. Траектория программы Comau сама по
себе не измеренный эталон. Электрическая и механическая безопасность требуют
проверки человеком.
