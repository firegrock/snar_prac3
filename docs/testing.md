# Воспроизводимые испытания

PASS — реально выполнено с логом; FAIL — выполнено, критерий нарушен;
NOT RUN — не выполнено с причиной. [TEST_REPORT](../TEST_REPORT.md) — фактический
результат этой машины/платы, а таблицы ниже — процедуры, не заранее полученный PASS.

## Автоматические программные

```sh
. .venv/bin/activate
sh scripts/test.sh
python -m snar_imu validate examples/synthetic/static
```

Native C проверяет signed/scales/SI/R_BS/det; known bias/scale, wrong face,
движение, NaN, UID; Mahony rest/tilt/yaw/sign/norm/q/-q/zero accel/dt/Ki;
четыре cursors, overwrite/GAP, concurrent producer/readers с production locking
contract, seq limit; C CRC/COBS/endian/64бит и emit fixture; production policy,
request cache и NVS version/size/CRC/UID. Python проверяет C wire-vector,
roundtrip/corruption/length/boot-noise/lost delimiter, disk dedup, identity,
gaps/order и compare. PTY-тест запускает **настоящий CLI** в дочернем процессе,
пропускает битый кадр, делает resume и Ctrl+C. PTY-устройство SYNTHETIC; на macOS
его условная скорость в тесте 115200 (PTY не реализует нестандартный baud ioctl),
это не испытание физического UART 460800.

Node при наличии запускает тот же CRC/C-vector/BigInt и corruption; на macOS
предусмотрен запуск того же browser codec во встроенном JavaScriptCore без npm.
Octave при наличии сравнивает с C fixture (double vs float, 2e-5 quaternion norm
с q/-q и 1e-5 integral). Если среды нет, script печатает NOT RUN, не «эквивалентный PASS».
Проверки actual HTTPD/radio/FIFO не подменяются программным имитатором.

Для проверки выбранной настоящей платы (меняет только ориентационный stream,
не сохраняет NVS; убедиться, что нет активного опыта):

```sh
python scripts/hardware_checks.py --serial /dev/cu.usbserial-110 --device-id 1 --seconds 15 --output hardware-checks-new.json
```

Дополнительные утилиты (новый каталог, **не одновременно на одном UART**):

```sh
python scripts/hardware_rates.py --serial /dev/cu.usbserial-110 --device-id 1 --output runs/rates_new
python scripts/hardware_resume.py --serial /dev/cu.usbserial-110 --device-id 1 --output runs/resume_new
python scripts/hardware_gyro_calibration.py --serial /dev/cu.usbserial-110 --device-id 1 --output runs/gyro_new --confirm-apply-save
python scripts/replay_c.py runs/static_01
python scripts/gyro_statistics.py runs/static_01
# Активирован ESP-IDF. Пять прошивок ОДНОЙ выбранной платы, в конце01:
sh scripts/check_images.sh /dev/cu.usbserial-110 runs/images_new
```

Gyro-утилита применяет/сохраняет прошедший критерии кандидат только при явном
флаге; не заменяет шесть физических граней. Rates-утилита проверяет короткий
unsubscribe/resume UART, не радио. Production C replay — проверка согласованности
записи, не замена C/Octave и не оценка точности датчика.

## Приёмка на столе — после ручной проверки wiring.md

1. Backup/flash_id, builds01..04, сверить compile definitions/SSID/UI/metadata/LCD.
   Одна плата под четырьмя сборками не равна четырём физическим комплектам.
2. Проверить readback MPU, отсутствие выдуманных raw при ошибке; INT/FIFO counts,
   временную привязку логическим анализатором, реальные каналы/знаки шести граней.
   WHO_AM_I сам по себе не доказывает все каналы.
3. При частотах50/100/200 выполнить `set_config` вне опыта, новый каталог на режим:

```sh
python -m snar_imu command --ws ws://192.168.4.1/ws --device-id 1 set_config --params '{"config":{"sample_rate_hz":50}}'
python -m snar_imu record --ws ws://192.168.4.1/ws --device-id 1 --duration 60 --output runs/rate50
python -m snar_imu validate runs/rate50
```

   Повторить со100/200 и отдельными каталогами. Проверять measured_rate/dt histogram,
   не требовать идеально10000мкс от внутреннего генератора MPU. Ring832 >=4с
   рассчитан, фактическую глубину проверить oldest/next и доступными временами.
4. Один владелец начинает опыт, USB и Wi-Fi записывают **одновременно**: второй
   `record --observe`. Затем `compare` по общим полным ключам. USB=open может дать
   reset — сначала открыть USB, затем подключать WS. Два каталога должны иметь
   один UID/boot/stream, иначе сравнение правильно отвергнется.
5. База100Гц: `record ... --duration 600`. Убедиться, что boot стабилен, нет WDT,
   free/min heap/stack не деградируют, cfg/rate не меняется, все sensor/transport
   gaps объяснены. Пустая запись или только диагностика не равна десятиминутному
   испытанию измерительного потока.
6. Четыре полных WS и USB, плюс страница диагностики. Сначала можно четыре процесса
   на одном ПК (проверка получателей), затем четыре **раздельных Wi-Fi станции**
   (реальный лимит AP). Записать heap/throughput. Клиент без ACK/чтения: остальных
   не должен блокировать; медленный клиент закрывается и восстанавливается.
7. Короткий socket pause/reconnect: запросить last_seq до flush, проверить replay
   совпадает с исходными q/raw/t_us. Длинный pause: точный GAP и продолжение.
   Отдельно отключить **радио** (не только сокет), вновь association/DHCP/WS/resume;
   отметить время обнаружения и начало resume. Полное4сек отключение не гарантируется.
8. Ctrl+C, потеря сокета/кабеля, resetESP: без смешивания boot/UID, новый сегмент.
   Старый ACK другого клиента не удаляет историю. Изменениеcfg между опытами —
   новый stream, старые метаданные не подменены. В активном опыте команда cfg
   должна отвергаться. Повтор request_id не повторяет reset/start/wizard.
9. Мастераgyro/six, движение/ошибочная грань отклонены, raw/statistics сохранены;
   candidate -> отдельные apply/save -> restart -> независимая проверочная запись.
   NVS неверной версии: программный тест production validator; физическое
   повреждение NVS выполнять лишь на специально выделенной плате с backup.
10. LCD отключать/переподключать только со снятым питанием; безLCD сенсор/AP работают.
    MPU ошибку проверять также при снятом питании перед переподключением, затем
    диагностировать новый запуск. Не вынимать провода под напряжением ради теста.

## Электрика и Comau — только человек

Отдельный лист результатов мультиметра: USB5V,3V3,MPU VDD,SDA/SCL обе стороны,
подтяжки, общаяземля. Отдельный лист крепежа/кабелей и разрешения преподавателя.
Никаких программных команд манипулятору/контроллеру. На роботе — только запись
существующего движения по правилам лаборатории; без измеренной телеметрии Comau
его программная траектория не эталон. Неудовлетворительный монтаж — STOP,
а не «пройдено, потому что пакеты пришли».
