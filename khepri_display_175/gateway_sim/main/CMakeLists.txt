idf_component_register(SRCS "main.c" "khepri_relay.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash esp-now khepri_proto)
