# Сторонние компоненты

- **ESP-IDF**, Espressif, установленная ревизия `v6.1-dev-8798-g4d80c1b29ae`.
  Основная лицензия Apache-2.0; отдельные bundled компоненты имеют свои лицензии
  в SDK. SDK не копируется в проект и не модифицируется.
- **cJSON**, Dave Gamble and contributors, MIT, component `espressif/cjson 1.7.19`,
  repository commit `721d625669f4e4fdfe6e02cf7e11f15b33f13e3a`, component hash
  `9372811fb197926f522c467627cf4a8e72b681e0366e17879631da801103aef3`.
  Загружается ESP-IDF Component Manager; полный текст MIT включён в
  `managed_components/espressif__cjson/cJSON/LICENSE`.
- **pyserial 3.5**, Chris Liechti, BSD-3-Clause,
  <https://github.com/pyserial/pyserial/tree/v3.5>.
- **websocket-client 1.8.0**, websocket-client contributors, Apache-2.0,
  <https://github.com/websocket-client/websocket-client/tree/v1.8.0>.

Исходники драйверов, SI codec, Mahony C/Octave, мастера, web и регистратора написаны
для этого проекта; сторонних готовых фильтров/DMP/Arduino-библиотек нет.
Страницы первичных технических источников перечислены в docs/sources.md.
