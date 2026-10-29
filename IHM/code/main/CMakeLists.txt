file(GLOB FONTS "fonts/*.c")
idf_component_register(
    SRCS "main.c" "khepri_screen.c" "khepri_link.c" "kh_board.c" "knob.c" "kh_wifi.c" "kh_ota.c" ${FONTS}
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_lcd esp_event esp_wifi esp_netif nvs_flash
             esp_http_client esp_https_ota app_update json mbedtls esp_app_format
             lvgl esp_lcd_sh8601 esp-now i2c_bsp qmi8658 khepri_proto)
