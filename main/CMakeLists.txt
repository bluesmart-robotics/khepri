idf_component_register(SRCS "main.c" "bsr_espnow_control.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer lvgl ui_bsp i2c_bsp esp_touch
                              espressif__esp_lcd_sh8601)
