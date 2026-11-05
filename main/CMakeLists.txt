idf_component_register(SRCS "app.c" "sensor.c" "lcd.c" "json_model.c" "commands.c" "transport.c"
    INCLUDE_DIRS "." REQUIRES lab_core esp_driver_i2c esp_driver_gpio esp_driver_uart esp_timer
    esp_wifi esp_netif esp_event esp_http_server nvs_flash espressif__cjson esp_hw_support esp_system
    EMBED_TXTFILES "../web/index.html" "../web/app.js" "../web/style.css")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LAB_DEVICE_ID=${LAB_DEVICE_ID})
idf_component_get_property(cjson_lib espressif__cjson COMPONENT_LIB)
target_compile_definitions(${cjson_lib} PRIVATE CJSON_NESTING_LIMIT=16)
