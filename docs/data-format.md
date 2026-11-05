# Файлы опыта и GNU Octave

Wire — binary SI/1; **CSV появляется на ПК**, это не переименование бинарного
потока. Один каталог — один UID/boot/stream/config. Все времена измерений — ESP32;
время приёма ПК только transport/events, не dt фильтра.

| Файл | Содержание |
|---|---|
| samples.csv | одна шапка, уникальные строки, по seq, точка в float |
| metadata.json | immutable config/calibration/versions/axes + initial_state |
| metadata_numeric.csv | одна строка числовых метаданных для Octave без JSON |
| events.jsonl | метки, connect/disconnect/GAP, device events/status/counters |
| transport.csv | host_unix_ns, transport, seq, replay, duplicate; отдельно от sample |
| quality.json | unique/received/recovered/duplicates, необратимые дыры, dt histogram, ошибки |
| index.sqlite3 | дисковый индекс уникальности и сортировки, вспомогательный |
| candidate.json / candidate_numeric.csv | результат режима calibrate, ещё не применён |

`samples.csv`:
`seq,t_us,ax,ay,az,gx,gy,gz,temperature,qw,qx,qy,qz,flags`.
Все raw signed int16, temperature тоже raw. q — float32 wire, сохраняется
точное представимое значение Python float без повторной фильтрации. UID/boot/stream
лежат строками metadata.json; не нужно помещать их в double CSV. Сравнение
двух транспортов сверяет полный ключ и t_us/raw/q/flags, не host arrival.

## metadata_numeric.csv — 42 столбца

| Номера Octave (1-based) | Значения |
|---|---|
| 1..7 | rate_hz,accel_lsb_g,gyro_lsb_dps,g0,Kp,Ki,use_gyro_bias |
| 8..10 | gyro_bias XYZ (S,rad/s) |
| 11..13 | accel_bias XYZ (S,m/s²) |
| 14..16 | accel_scale XYZ |
| 17..25 | R_BS, row-major |
| 26..29 | q_initial qw,qx,qy,qz |
| 30..32 | integral_initial XYZ |
| 33..36 | before_seq,previous_t_us,ready,still_n |
| 37..39 | still_mean XYZ |
| 40..42 | still_m2 XYZ |

До before_seq — состояние начального snapshot, после samples[k] — q[k].
Для чужого наблюдателя поздно присоединившегося исходная граница может быть уже
недоступна. `replay_run` тогда отказывает в точном replay, а не подставляет q=identity.
Время t_us на практике точно помещается в double при времени жизни boot <285 лет;
загрузчик явно отвергает >flintmax. 64-битные UID/boot никогда не загружаются в double.

## Octave

Целевая версия Ubuntu20.04 — Octave5.2, используются базовые dlmread/fopen/plot,
не readtable/jsondecode/toolbox. Фактический запуск зависит от установленного
Octave и отражён в TEST_REPORT; отсутствие исполняемого файла не считается PASS.

```octave
addpath('octave'); addpath('octave/reference');
r=snar_load('examples/synthetic/static'); % SYNTHETIC, НЕ измерения
snar_plot(r);
q=replay_run(r);
test_fixture('tests/out/mahony_fixture.csv'); % после sh scripts/test.sh
```

`snar_check_time` разделяет участки при seq/time gaps/флаге16; интеграторы обязаны
уважать эти границы. График углов не отображает неготовую ориентацию как нулевую.
Установка/масштаб R_BS проверяются. Калибровочные CSV: 10 столбцов
`gbx,gby,gbz,abx,aby,abz,asx,asy,asz,temperature_c`; helpers load/save и six solver.
`student_templates/` содержит только учебные инструкции, не незавершённые драйверы.

quality `NO_DETECTED_GAPS` означает отсутствие **обнаруженных** нарушений, а не
метрологическую гарантию целостности. frame_errors включает отброшенный ROM-мусор;
отдельные DEVICE_STATUS показывают доформировочные sensor counters. Перерыв между
seq отражается как необратимая дыра только если не восстановлен из RAM; восстановленные
пакеты/дубликаты считаются отдельно. Начальная недоступная история также фиксируется
GAP/IRRECOVERABLE в events, а не незаметно объявляется полной записью.
