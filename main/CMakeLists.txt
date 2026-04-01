idf_component_register(SRCS "main.c" "bsr_espnow_control.c" "battery_app.c" "battery_unplugged_app.c" "bsr_battery_adc.c" "bsr_ota.c" "bsr_wifi_config.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer lvgl ui_bsp i2c_bsp esp_touch
                              espressif__esp_lcd_sh8601 esp_event nvs_flash qmi8658 esp_adc
                              app_update esp_http_client esp_wifi)
