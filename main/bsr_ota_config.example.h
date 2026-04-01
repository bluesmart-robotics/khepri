/*
 * bsr_ota_config.example.h — Copier vers bsr_ota_config.h et remplir
 *
 * Ce fichier est un modele. Pour l'utiliser :
 *   1. Copier ce fichier : cp bsr_ota_config.example.h bsr_ota_config.h
 *   2. Remplir avec tes identifiants WiFi et l'IP de ton PC
 *   3. bsr_ota_config.h est dans le .gitignore, il ne sera pas commite
 */

#ifndef BSR_OTA_CONFIG_H
#define BSR_OTA_CONFIG_H

#define BSR_OTA_WIFI_SSID       "NOM_DE_TA_BOX"
#define BSR_OTA_WIFI_PASS       "MOT_DE_PASSE_BOX"
#define BSR_OTA_FW_URL          "http://192.168.1.XX:8080/bsr_espnow_control.bin"

#endif
