/*
 * bsr_wifi_config.h — Stockage WiFi en NVS
 *
 * Sauvegarde et charge les identifiants WiFi (SSID + password)
 * dans la memoire non-volatile de l'ESP32.
 * Remplace le fichier bsr_ota_config.h (plus de hardcode).
 */

#ifndef BSR_WIFI_CONFIG_H
#define BSR_WIFI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#define BSR_WIFI_SSID_MAX  33   /* 32 chars + null */
#define BSR_WIFI_PASS_MAX  65   /* 64 chars + null */

/* Initialise le namespace NVS. Appeler une fois au demarrage. */
void bsr_wifi_config_init(void);

/* Sauvegarde les identifiants WiFi en NVS. Retourne true si OK. */
bool bsr_wifi_config_save(const char *ssid, const char *password);

/* Charge les identifiants WiFi depuis NVS.
 * ssid et password doivent etre de taille BSR_WIFI_SSID_MAX et BSR_WIFI_PASS_MAX.
 * Retourne true si des identifiants existent. */
bool bsr_wifi_config_load(char *ssid, size_t ssid_len,
                          char *password, size_t pass_len);

/* Retourne true si des identifiants WiFi sont sauvegardes en NVS. */
bool bsr_wifi_config_exists(void);

/* Efface les identifiants WiFi de la NVS. */
void bsr_wifi_config_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* BSR_WIFI_CONFIG_H */
