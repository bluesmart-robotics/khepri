/*
 * bsr_ota.h — Mise a jour OTA via WiFi HTTP
 *
 * Se connecte temporairement a un AP WiFi, telecharge le firmware
 * depuis une URL HTTP, ecrit dans la partition OTA inactive,
 * puis reboot sur la nouvelle version.
 *
 * Compatible avec ESP-NOW (meme mode STA).
 * Les identifiants WiFi sont lus depuis la NVS (bsr_wifi_config).
 */

#ifndef BSR_OTA_H
#define BSR_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* URL du firmware (peut etre un serveur local ou GitHub Releases) */
#define BSR_OTA_FW_URL  "http://192.168.1.11:8080/bsr_espnow_control.bin"

/* Timeout connexion WiFi (ms) */
#define BSR_OTA_WIFI_TIMEOUT_MS 30000

/* Lance la mise a jour OTA.
 * 1. Lit SSID/password depuis NVS
 * 2. Se connecte au WiFi
 * 3. Telecharge le firmware depuis BSR_OTA_FW_URL
 * 4. Ecrit dans la partition OTA inactive
 * 5. Reboot si succes
 *
 * Retourne false si echec (pas de config WiFi, connexion, download).
 * Note: si succes, la fonction ne retourne pas (reboot). */
bool bsr_ota_start(void);

/* Retourne la version du firmware en cours (app description) */
const char *bsr_ota_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* BSR_OTA_H */
