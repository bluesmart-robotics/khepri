file(GLOB FONTS "fonts/*.c")
idf_component_register(
    SRCS "main.c" "khepri_screen.c" "khepri_link.c" "kh_board.c" "knob.c" ${FONTS}
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_lcd esp_event esp_wifi esp_netif nvs_flash
             lvgl esp_lcd_sh8601 esp-now i2c_bsp qmi8658 khepri_proto)
