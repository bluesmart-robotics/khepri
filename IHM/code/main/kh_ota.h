/*
 * kh_ota.h — mises à jour automatiques depuis GitHub.
 *
 * Quand l'écran est connecté à un réseau Wi-Fi, il lit toutes les minutes le manifeste
 *   https://raw.githubusercontent.com/bluesmart-robotics/khepri/main/IHM/code/release/version.json
 *   { "version": "1.1.0", "url": "https://.../khepri_ecran_app.bin", "size": 1234567 }
 * Si "version" diffère de la version du firmware en cours (PROJECT_VER), il télécharge l'image en
 * HTTPS (certificats racine intégrés), l'écrit dans l'autre partition OTA et redémarre dessus.
 * La nouvelle image se valide après 20 s de fonctionnement ; sinon le chargeur revient à l'ancienne.
 *
 * Jamais pendant que les moteurs tournent ou qu'une alerte est affichée (rappel can_update).
 * Le CDN de GitHub garde les fichiers jusqu'à 5 min en cache : une version publiée est vue en
 * 1 à 6 minutes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KH_OTA_IDLE = 0,    /* pas de Wi-Fi */
    KH_OTA_UP_TO_DATE,  /* manifeste lu, même version */
    KH_OTA_CHECK_FAIL,  /* manifeste illisible */
    KH_OTA_DOWNLOADING,
    KH_OTA_FAILED,      /* téléchargement ou écriture en échec */
    KH_OTA_DEFERRED,    /* nouvelle version, mais moteurs en marche */
} kh_ota_state_t;

typedef struct {
    kh_ota_state_t state;
    char running[32];   /* version en cours */
    char remote[32];    /* version annoncée par GitHub */
    int progress_pct;
    int64_t last_check_ms;
} kh_ota_status_t;

typedef bool (*kh_ota_can_update_cb_t)(void);

void kh_ota_init(kh_ota_can_update_cb_t can_update);
/* Force une vérification immédiate */
void kh_ota_check_now(void);
void kh_ota_get_status(kh_ota_status_t *out);
/* À appeler une fois l'application jugée saine (annule le retour arrière) */
void kh_ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
