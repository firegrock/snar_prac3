# Первичные источники и характер утверждений

**Факт datasheet:** ESP32 имеет два I²C-контроллера; MPU ranges, register layout,
FIFO, DLPF/sample dividers, преобразование температуры; PCF адресные диапазоны;
open-drain двунаправленное согласование. Эти факты не подтверждают конкретный
монтаж, реальность маркировки клона или разводку купленного LCD.

**Проектное решение:** SoftAP/WPA2, оси/кватернион, SI/1, 832 RAM-records,
IRQ/FIFO resync, bounded cursors, software calibration, no flash stream.

**Стартовые параметры:**100Гц,±4g,±500°/с,DLPF4,SMPLRT_DIV9,Kp2,Ki0,
пороги покоя/движения. Они не объявлены экспериментально оптимальными.

**Измеренный результат:** только записи со статусом PASS/FAIL в TEST_REPORT и
сопровождающие логи. SYNTHETIC-файлы не относятся к аппаратным измерениям.

## SDK именно установленной ревизии

Локально проверены:
`$IDF_PATH/components/esp_driver_i2c/include/driver/i2c_master.h`,
`esp_driver_uart/include/driver/uart.h`,
`esp_http_server/include/esp_http_server.h`, `esp_wifi/include/esp_wifi.h`,
`esp_timer/include/esp_timer.h` в commit `4d80c1b29ae`.
Репозиторий: <https://github.com/espressif/esp-idf/tree/4d80c1b29ae>.
Указатели документации (stable — точка входа, не требование обновиться):

- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html>
- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html>
- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html>
- <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html>
- <https://docs.espressif.com/projects/esp-faq/en/latest/hardware-related/hardware-design.html>

## Сенсор/согласование/алгоритм

- MPU-6000/6050 Register Map and Descriptions rev4.2:
  <https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf>.
  В этой среде исходный URL вернул страницу поиска TDK вместо PDF; это не проверка
  содержимого PDF. Перед аппаратной приёмкой получить именно документ rev4.2,
  сверить MPU на экземпляре. Не считать переход сайта подтверждением datasheet.
  Дополнительно прочитана копия InvenSense Register Map **rev4.0** на
  <https://cdn.sparkfun.com/datasheets/Sensors/Accelerometers/RM-MPU-6000A.pdf>:
  поля FIFO/диапазонов/делителя/температуры. Она не выдаётся за требуемую rev4.2.
- PCF8574/PCF8574A NXP:
  <https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf>.
  Прочитана rev5, 27 May 2013: Standard-mode100кГц, quasi-bidirectional ports,
  разные фиксированные части адресов PCF8574/PCF8574A. Это не идентификация LCD.
- Nexperia AN10441:
  <https://assets.nexperia.com/documents/application-note/AN10441.pdf>.
  URL ответил HTTP403 в этой среде; электрический монтаж требует проверки человеком.
- Mahony, Hamel, Pflimlin, *Nonlinear Complementary Filters on the Special
  Orthogonal Group*:
  <https://researchportalplus.anu.edu.au/en/publications/nonlinear-complementary-filters-on-the-special-orthogonal-group/>.

Численная реализация здесь оригинальная, явно задана формой из ТЗ и тестируется
на знак/соглашения; случайный сторонний код с twoKp/twoKi не копировался.
