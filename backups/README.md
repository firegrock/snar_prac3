# Локальный backup до перезаписи

Плата на `/dev/cu.usbserial-110`: ESP32-D0WD-V3 rev3.1, base MAC
`2c:bc:bb:0d:72:50`; esptool5.6.0: flash4MB, manufacturer68/device4016.

- Попытка460800: **FAIL**, `Corrupt data, expected 0x1000 bytes but received 0xf9c`.
  Частичный файл, если остался, не является успешным backup.
- Попытка115200: **PASS**, `esp32_2cbcbb0d7250_before_115200.bin`,
  **4194304 байта**, SHA-256
  `a788f570240b6bc1e5753bec40feed9af606cb7cf34557da57a4098cc4dcde07`.

Это полный read-flash до первой прошивки проекта, не backup одной app-partition.
Содержимое не публиковалось. Восстановление из дампа не выполнялось/не проверялось.
eFuse не изменялись, erase-flash не выполнялся. Не восстанавливайте чужой дамп
на другую плату автоматически.
