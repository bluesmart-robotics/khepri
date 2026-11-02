/*
 * kh_ota.h — mises à jour automatiques depuis GitHub.
 *
 * Quand l'écran est connecté à un réseau Wi-Fi, il lit toutes les minutes SON manifeste
 * (KHEPRI_OTA_MANIFEST_URL : release/ecran_rond/version.json ou release/ecran_energie/version.json)
 *   { "version": "1.5.0", "url": "https://.../image.bin" }        un seul type de carte
 *   { "version": "1.0.0", "url_V1": "…", "url_V1.1": "…" }        un champ par révision de carte
 * Le champ lu est KHEPRI_OTA_URL_FIELD.
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

/* ── Écran énergie : mise à jour de l'écran rond par ESP-NOW ──
 * Si KHEPRI_OTA_PEER_MANIFEST_URL n'est pas vide, la tâche OTA lit aussi le manifeste du rond ;
 * quand la version publiée diffère de celle que le rond annonce (kh_peer), elle télécharge
 * l'image dans la partition « fw_rond » (à défaut, dans sa propre partition OTA libre), en
 * vérifie le SHA-256, la propose au rond (moteurs arrêtés, pas d'alerte) puis la lui envoie
 * avec la bibliothèque esp-now OTA. */
typedef struct {
    uint8_t state;      /* KHP_UPD_* */
    uint8_t pct;        /* téléchargement ou envoi */
    char remote[32];    /* version publiée pour le rond */
} kh_ota_peer_status_t;
void kh_ota_get_peer_status(kh_ota_peer_status_t *out);

/* ── Écran rond : réception d'une mise à jour poussée par l'écran énergie ──
 * kh_ota_rx_accept() arme la réception (esp-now OTA, vers la partition OTA libre) ; la
 * progression arrive par le rappel ; à la fin, l'écran redémarre sur la nouvelle version.
 * kh_ota_rx_tick() (1 Hz) désarme la réception si plus rien n'arrive pendant 3 minutes. */
typedef void (*kh_ota_rx_cb_t)(int pct, bool done, bool failed);
void kh_ota_rx_accept(kh_ota_rx_cb_t cb);
void kh_ota_rx_tick(void);
bool kh_ota_rx_active(void);

#ifdef __cplusplus
}
#endif
