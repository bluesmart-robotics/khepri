# Commun aux deux écrans KHÉPRI : liaison ESP-NOW avec le maître, Wi-Fi, mises à jour OTA
idf_component_register(
    SRCS "khepri_link.c" "kh_peer.c" "kh_wifi.c" "kh_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer esp_http_client esp_https_ota
             app_update esp_partition json mbedtls esp_app_format esp-now khepri_proto)
