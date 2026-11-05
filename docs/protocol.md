# Протокол SI/1

Все целые little-endian; signed raw — two's complement int16; q — IEEE754
binary32. C-структуры **не** являются wire format. Одна неизменяемая запись
`device_uid + boot_id + stream_id + seq` используется USB, Wi-Fi и replay.
`t_us` — signed-положительное монотонное время ESP32 около DRDY, не время ПК.

## Внутренний пакет

| Смещение | Размер | Поле |
|---:|---:|---|
| 0 | 2 | ASCII `SI` (53 49 hex) |
| 2 | 1 | версия 1 |
| 3 | 1 | type: 1 SAMPLES, 2 COMMAND, 3 RESPONSE, 4 EVENT, 5 HELLO |
| 4 | 2 | total_length, включая заголовок и CRC |
| 6 | 2 | delivery_flags, bit0 REPLAY; не flags измерения |
| 8 | 8 | device_uid (MAC SoftAP SDK, числовое значение 48 бит) |
| 16 | 8 | boot_id, случайный при каждом запуске |
| 24 | 8 | stream_id, случайный при значимой смене конфигурации |
| 32 | 4 | config_id, номер конфигурации внутри boot |
| 36 | N | payload |
| 36+N | 4 | CRC-32/ISO-HDLC всех предшествующих байтов [0,total_length-4) |

Заголовок 36, минимальный пакет 40, максимум **6184 байта**. CRC: reflected
poly 0xEDB88320, init/xorout 0xFFFFFFFF, check(`123456789`)=**0xCBF43926**.
CRC защищает транспорт, не подтверждает правильность сенсора/временных меток.

### SAMPLES

Payload: uint16 count (1..8), затем count записей по **46 байт**:

| Относительное смещение | Тип | Поле |
|---:|---|---|
| 0 | uint32 | seq |
| 4 | uint64 | t_us |
| 12,14,16 | int16 | ax, ay, az |
| 18,20,22 | int16 | gx, gy, gz |
| 24 | int16 | temperature |
| 26,30,34,38 | float32 | qw,qx,qy,qz, состояние ПОСЛЕ этой выборки |
| 42 | uint32 | sample flags |

Размер пакета `42 + 46*count`: 88..410. Raw в S, **до программной** коррекции,
но **после встроенного DLPF**. Масштабы/диапазоны/температура — metadata.

Sample flags: 1 ORIENTATION_INVALID, 2 ACCEL_INVALID (нет нормализации),
4 ACCEL_NORM (отклонение >20% g0), 8 SATURATED (|raw| около границы),
16 TIME_GAP/resync, 32 UNCALIBRATED, 64 CALIBRATION. Старый/условный q при bit0
**не является текущей достоверной ориентацией**. REPLAY только в заголовке.

### USB

UART0, GPIO1 TX/GPIO3 RX, **460800, 8N1**, без flow control. Внутренний пакет
кодируется COBS, затем байт **00**. Max COBS-кадр с разделителем <=6211 байт.
COMMAND payload — UTF-8 JSON до **4096 байт**, остальные JSON до 6144.
HELLO раз в секунду до подписки. Один писатель, обычный app/bootloader log отключён.
ROM всё равно может печатать на 115200: декодер отбрасывает мусор до валидного
COBS/CRC/HELLO; после open ждёт HELLO и только потом отправляет get_info.
Потерянный разделитель портит объединённый кадр, следующий валидный восстанавливает
синхронизацию. Нет команды намеренного reset DTR/RTS в регистраторе.

### Wi-Fi

`ws://192.168.4.1/ws`. Один binary message — один **внутренний** SAMPLES-пакет,
без COBS/разделителя. JSON commands/responses/events — отдельные text messages,
не встроенные бинарные пакеты. Не допускаются фрагментированные команды, текст
>4096 байт. GET `/api/info`, `/api/status` только читают состояние. Все изменения
только через диспетчер WS/UART. Содержимое страницы/JS/CSS встроено во flash.

## JSON

**UID, boot_id, stream_id, все 64-битные времена — десятичные строки**. JS binary
читает BigInt. seq/config_id/count — uint32 и точно представимы в JS Number.
Обязательны `request_id` и `client_id`, непустые ASCII-совместимые строки до
63 байт. Команды чувствительны к написанию приведённых имён; не используйте
альтернативный регистр полей. Не допускаются NaN/Infinity, NUL, глубокая вложенность
>16. Человекочитаемый текст метки <=96 UTF-8 байт, сериализуется JSON-экранированием.

```json
{"request_id":"pc01-1","client_id":"pc01","command":"get_info"}
```

Ответ: `{"request_id":"pc01-1","ok":true,"result":{...}}`, либо
`{"request_id":"pc01-1","ok":false,"error":{"code":"INVALID_STATE","message":"..."}}`.
Мутации требуют **ещё** `device_id` и `device_uid`, сверенные read-only handshake.
При несовпадении не начинать опыт. Клиент сохраняет свой client_id при reconnect.

### Команды

| command | Дополнительные поля / результат |
|---|---|
| get_info | номер, UID, boot/stream, версии, ограничения |
| get_status | sensor/orientation, counters, RAM/стеки, опыт, wizard |
| get_config | полный metadata, включая calibration и initial_state |
| subscribe | UID/boot/stream; `mode: live|history`; опциональный `from_seq` |
| resume | UID/boot/stream, `last_seq` последней непрерывно записанной записи |
| unsubscribe | прекратить поток только этому получателю |
| ack | `seq`, после flush файлового буфера |
| get_events | `after` ID следующего события; до 16 за ответ; `next_event`, `detail_unavailable` |
| start_experiment | фиксация immutable snapshot на границе before_seq, owner=client_id |
| end_experiment | другой владелец: явное `confirm_owner:true` |
| mark | `text`; t_us/seq присваивает модуль, ПК отдельно пишет своё время |
| set_config | `config`: sample_rate_hz, accel_range_g, gyro_range_dps, dlpf, Kp, Ki, g0, R_BS[9], use_gyro_bias |
| cal_gyro | неблокирующий мастер 30 с |
| cal_accel | начать мастер шести граней, ожидать cal_face |
| cal_face | `face`: 0 +X,1 -X,2 +Y,3 -Y,4 +Z,5 -Z; собирать 5 с |
| cal_cancel | отменить сбор; не применяется автоматически |
| cal_export | `source: active|candidate`; JSON схемы 1 |
| cal_import | `calibration` объект; чужой UID только `confirm_transfer:true`; результат кандидат |
| cal_apply | явно применить кандидата, новый stream |
| cal_save | явно записать действующие коэффициенты NVS |
| cal_reset | безопасные коэффициенты UNCALIBRATED, новый stream; не удаляет flash автоматически |
| reset_orientation | полный сброс, новый stream |
| zero_yaw | согласованное левое умножение на мировой Rz(-yaw), новый stream |
| lcd | `page` 0..4, `backlight` bool |

В активном опыте разрешены чтение, подписка, ACK, mark, LCD, явный конец. Остальное
INVALID_STATE; при активном мастере конфликтующие изменения BUSY. Потеря владельца
не завершает опыт. start повторно тем же владельцем не меняет snapshot. Второй
наблюдатель подписывается без start.

В event/mark `t_us` — время обработки события на устройстве, `seq` — ближайшая
граница формирования (следующая запись для команды mark). Это не утверждение,
что внешнее физическое действие произошло точно на фронте DRDY этого seq.

Мутации сериализованы одним диспетчером. Последние **16** ответов мутаций
кэшируются по client_id/request_id + CRC команды; тот же ID с другими байтами
REQUEST_ID_REUSED. Повтор RPC использует ровно те же байты. Этот bounded cache
не является бессрочной журналируемой гарантией exactly-once: после reboot/вытеснения
сверьте состояние и используйте новое явное действие; не повторяйте древние ID.
Длительные start/wizard также защищены текущим состоянием. Системы аккаунтов нет.

Ошибки: BAD_COMMAND, UNKNOWN_COMMAND, IDENTITY_MISMATCH, STREAM_BOUNDARY,
INVALID_CURSOR, INVALID_CONFIG, INVALID_CALIBRATION, INVALID_STATE, BUSY, NVS_ERROR.
При переполнении входной очереди — BUSY либо отказ транспортного кадра, RPC timeout
с повтором того же ID. Некорректные COBS/CRC не проходят в диспетчер.

## Границы/метаданные/resume

Подписка возвращает metadata **до** samples, requested_seq/oldest_seq/next_seq и
gap. Metadata содержит диапазоны/чувствительности, g0, R_BS, Kp/Ki, калибровку,
версии, соглашения, очередь IRQ/baud, начальное состояние q/integral/previous_t_us,
ready и состояние подтверждения покоя, `before_seq`. Начало опыта не сбрасывает
фильтр. При изменении настроек новый stream, ring очищен; старые метаданные
не подменяются текущими. Непрерывный поток старого подписчика останавливается с
STREAM_BOUNDARY. Нужны новые metadata и подписка, а на ПК — новый сегмент.

Resume не изменяет сохранённые raw/q/t_us/flags. У каждого получателя свой cursor;
ACK не удаляет ring. Если cursor вытеснен, EVENT `GAP` содержит from_seq, oldest_seq,
next_seq и причину, затем досылается доступная история. При reboot прежние boot/stream
отвергаются. Подробности старых событий хранятся отдельно в ring 64 событий;
при их вытеснении `detail_unavailable`, а суммарные counters остаются в status.
Детали не изобретаются. Подписчик обязан получать события через get_events.
Каждое сохранённое событие имеет свой stream_id/config_id; UID/boot_id включены
в ответ get_events. Поэтому старые события не получают идентичность нового потока.

## Бюджет канала

Штатный pump каждые 5 мс отсылает доступную пачку, **не ждёт накопления 8**;
нормальная добавленная задержка пакетирования <=5 мс (при свободном планировщике).
Худший случай — один sample/пакет: 88 внутренних + COBS/00 <=90 байт, то есть
4500/9000/18000 байт/с для 50/100/200 Гц; 8N1 — 45/90/180 кбит/с против 460,8.
Остаётся >2,5-кратный запас на 200 Гц без учёта редких метаданных/команд.
При пачках из 8 — до 412*25=10300 байт/с на 200 Гц. Во время быстрой replay
используется запас UART; доступный TCP/радио запас требует аппаратного измерения.
USB без подписки не печатает полный поток. Отсутствие ACK >2,5 с останавливает
полную подписку; медленный WS не удерживает сервер на блокирующей отправке.
