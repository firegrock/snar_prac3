# macOS: SDK, сборка и прошивка

## Реально найдено

- SDK: `/Users/firegrock/esp/esp-idf`.
- `idf.py --version`: **v6.1-dev-8798-g4d80c1b29ae**,
  полный commit `4d80c1b29aeabc8e735ffc6e1be9bb23e95d1d5d`.
- `export.sh` сообщает 6.2, component manager — 6.2.0. Это разные представления
  одной установленной ревизии, не заявление о выпуске stable 6.2.
- Python SDK 3.11.18; Xtensa GCC 16.1.0, пакет `esp-16.1.0_20260609`.
- cJSON `espressif/cjson ==1.7.19`, SHA256 component hash — `dependencies.lock`.
- Никакого обновления SDK, переключения Git-ветки или глобальной установки не делалось.

Размеры и SHA-256 четырёх итоговых app-образов сохранены в
`protocol/build_manifest.json`; результаты проверок — `TEST_REPORT.md`.

API проверялись по заголовкам именно этой установки: новый `i2c_master.h`,
`esp_http_server.h`, `esp_wifi.h`; используется `WIFI_BW20`, не устаревшее имя.
Для воспроизводимости на другой машине нужен тот же commit SDK и его штатные
подмодули/инструменты. Совместимость с произвольным stable 5.x здесь не заявлена.

## Команды (из корня проекта)

```sh
. "$HOME/esp/esp-idf/export.sh"
idf.py --version
idf.py -B build-01 -DLAB_DEVICE_ID=1 build
idf.py -B build-02 -DLAB_DEVICE_ID=2 build
idf.py -B build-03 -DLAB_DEVICE_ID=3 build
idf.py -B build-04 -DLAB_DEVICE_ID=4 build
```

`LAB_DEVICE_ID` — единственный источник номера: cache CMake -> compile definition
в `main`. Допустимы только 1..4. Не sdkconfig/NVS. В compile_commands.json
проверяется `-DLAB_DEVICE_ID=N`. Раздельные каталоги не дают случайно прошить
старую конфигурацию. GPIO/адреса/частоты шин/UART/каналы — `main/lab_config.h`;
измерительные настройки — `lab_defaults()` и проверяемые команды.

```sh
# Сначала ports, сверить VID/PID/маркировку и физическую плату.
python -m snar_imu ports
python -m esptool --port /dev/cu.usbserial-110 chip-id
python -m esptool --port /dev/cu.usbserial-110 flash-id
# Определив размер 4 MB, ДО первой перезаписи:
python -m esptool --port /dev/cu.usbserial-110 --baud 115200 read-flash 0 0x400000 backups/board_before.bin
shasum -a 256 backups/board_before.bin

# Одна выбранная плата на выбранном порту; последовательно заменяйте платы.
idf.py -B build-01 -p /dev/cu.usbserial-110 -b 115200 flash
idf.py -B build-02 -p /dev/cu.usbserial-110 -b 115200 flash
idf.py -B build-03 -p /dev/cu.usbserial-110 -b 115200 flash
idf.py -B build-04 -p /dev/cu.usbserial-110 -b 115200 flash
```

Это **четыре отдельные операции**, не нужно прошивать подряд один модуль всеми
номерами при подготовке комплектов. На Ubuntu замените порт на проверенный
`/dev/ttyUSB0`. Flash-инструмент намеренно делает reset; регистратор — нет.
Закройте serial monitor/регистратор перед esptool. `idf.py monitor` не является
декодером бинарного UART. Не выполнять `erase-flash`, не менять eFuse, secure boot
или flash encryption. Полный дамп хранить локально, не публиковать.

При другой ёмкости flash остановиться и проверить плату/таблицу разделов.
Здесь обнаружена ESP32-D0WD-V3 rev3.1, MAC base `2c:bc:bb:0d:72:50`, 4 МБ.
Проект использует MAC SoftAP SDK как UID (может отличаться от base MAC).
Наличие ESP32 не подтверждает маркировку/питание внешнего модуля MPU/LCD.
