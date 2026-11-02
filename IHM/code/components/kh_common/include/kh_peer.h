/*
 * kh_peer.h — liaison ESP-NOW automatique entre l'écran rond et l'écran énergie.
 *
 * Au démarrage, chaque écran annonce son rôle (HELLO) ; le premier écran de l'autre rôle entendu
 * à moins de KHP_PAIR_RSSI devient son partenaire, sans bouton, jusqu'au prochain redémarrage
 * (ou RÉAPPAIRER). Ensuite :
 *   - le rond relaie à l'écran énergie la télémétrie qu'il reçoit du maître, ou qu'il simule ;
 *   - l'écran énergie, seul connecté au Wi-Fi, annonce l'état de la mise à jour du rond et lui
 *     pousse le nouveau firmware (kh_ota.c, bibliothèque esp-now OTA).
 *
 * Canal : connecté à un point d'accès, l'écran énergie suit le canal de ce point d'accès. Le rond,
 * s'il n'entend plus son partenaire et n'a pas de maître, parcourt les canaux pour le retrouver.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "khepri_espnow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool paired;      /* un partenaire a été trouvé depuis le démarrage */
    bool alive;       /* entendu dans les 3 dernières secondes */
    bool hunting;     /* le rond parcourt les canaux pour le retrouver */
    uint8_t mac[6];
    int8_t rssi;
    uint8_t channel;  /* canal radio courant de cet écran */
    khp_hello_t hello; /* dernier HELLO reçu du partenaire */
} kh_peer_status_t;

/* Télémétrie (paquets KHN) reçue du partenaire */
typedef void (*kh_peer_rx_cb_t)(const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi);
/* Rond : proposition de mise à jour ; renvoie KHP_REFUSE_NONE pour accepter */
typedef uint8_t (*kh_peer_offer_cb_t)(const khp_offer_t *o);

/* Après khepri_link_init() */
void kh_peer_init(uint8_t role, kh_peer_rx_cb_t rx);
/* À appeler à 10 Hz : HELLO, perte du partenaire, recherche de canal */
void kh_peer_tick(void);
/* Rond : le parcours des canaux est permis (pas de maître, pas de Wi-Fi) */
void kh_peer_allow_hunt(bool allow);
/* Contenu des prochains HELLO (champs propres à cet écran) */
void kh_peer_set_hello(uint8_t flags, uint8_t upd_state, uint8_t upd_pct, const char *upd_ver, const char *ssid);
/* Rond → énergie : relaie un lot de trames ; sans effet si pas de partenaire */
void kh_peer_send_frames(const khn_frame_t *f, int n, uint8_t flags);
void kh_peer_get(kh_peer_status_t *out);
bool kh_peer_alive(void);
/* Oublie le partenaire et en cherche un nouveau */
void kh_peer_repair(void);

/* Rond : traitement des propositions de mise à jour */
void kh_peer_on_offer(kh_peer_offer_cb_t cb);
/* Énergie : propose une mise à jour au rond et attend sa réponse (bloquant, ≤ timeout_ms).
 * Renvoie KHP_REFUSE_NONE si le rond accepte, sinon la raison (KHP_REFUSE_BUSY sans réponse). */
uint8_t kh_peer_offer(const khp_offer_t *o, int timeout_ms);

#ifdef __cplusplus
}
#endif
