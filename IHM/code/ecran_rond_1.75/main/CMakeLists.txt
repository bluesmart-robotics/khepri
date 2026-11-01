idf_component_register(
    SRCS "main.c" "khepri_screen.c" "kh_board.c" "knob.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_lcd esp_event esp_wifi esp_netif nvs_flash app_update esp-now
             lvgl esp_lcd_sh8601 i2c_bsp qmi8658 khepri_proto kh_common kh_fonts)
