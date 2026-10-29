/*
 * kh_model.h — KHÉPRI : état de l'avion vu par l'écran pilote, reconstruit à partir des trames CAN
 * relayées par ESP-NOW. Sans dépendance ESP-IDF (utilisé aussi pour les aperçus sur PC).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "khepri_espnow.h"
#include "kp_frames.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_STALE_MS 3000      /* au-delà, une donnée est affichée « --- » */
#define KH_LINK_LOST_MS 2000  /* au-delà, la liaison radio est déclarée perdue */
#define KH_ALARM_MUTE_MS 30000 /* après acquittement, même chaussette muette pendant 30 s */

/* Seuils vibratoires par défaut (paramètres warn_mg / alarm_mg des chaussettes, provisoires) */
#define KH_VIB_WARN_MG 3000
#define KH_VIB_ALARM_MG 6000

typedef struct {
    int64_t t_status, t_rms, t_order, t_pv, t_alarm; /* ms de la dernière réception, 0 = jamais */
    kp_status_t status;
    kp_vib_rms_t rms;
    kp_vib_order_t order;
    kp_pv_t pv;
    kp_alarm_t alarm;
    int64_t mute_until;
} kh_node_t;

typedef struct {
    kh_node_t node[KP_MAX_NODES];

    /* Régime des moteurs (BSR Drive, Get_Encoder_Estimates) */
    uint8_t odrv_node[2]; /* nœuds CANSimple des axes M0 / M1 (0 et 1 par défaut) */
    int64_t t_motor[2];
    float rpm[2];

    /* Heure commune du maître */
    int64_t t_sync;
    kp_sync_t sync;

    /* Radio */
    int64_t t_rx;
    uint32_t rx_pkts, lost_pkts;
    uint16_t last_seq;
    bool seq_valid;
    int8_t rssi;
    bool simulated; /* le dernier paquet portait KHN_HF_DEMO */

    /* Alarme vibratoire en attente d'acquittement par le pilote */
    bool alarm_pending;
    uint8_t alarm_node;
    kp_alarm_t alarm_last;
    int64_t alarm_t;
} kh_model_t;

void kh_model_init(kh_model_t *m);
/* Une trame CAN reçue (directement ou relayée) */
void kh_model_apply_frame(kh_model_t *m, uint16_t id, const uint8_t *d, uint8_t dlc, int64_t now_ms);
/* Un paquet ESP-NOW reçu : statistiques radio, puis chacune de ses trames */
void kh_model_apply_packet(kh_model_t *m, const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi, int64_t now_ms);
/* Le pilote a vu l'alarme */
void kh_model_ack_alarm(kh_model_t *m, int64_t now_ms);

static inline bool kh_fresh(int64_t t, int64_t now_ms, int64_t max_ms) { return t > 0 && now_ms - t <= max_ms; }
static inline bool kh_link_ok(const kh_model_t *m, int64_t now_ms) { return kh_fresh(m->t_rx, now_ms, KH_LINK_LOST_MS); }
/* RMS vectoriel (mg) d'une chaussette */
float kh_vib_vec_mg(const kp_vib_rms_t *r);
/* 0 normal, 1 avertissement, 2 alarme, −1 inconnu */
int kh_vib_level(const kh_node_t *n, int64_t now_ms);
/* Pourcentage de paquets perdus depuis le démarrage */
float kh_loss_pct(const kh_model_t *m);

/* ------------------------------------------------------------------ simulation
 * Génère un jeu complet de trames plausibles pour l'instant now_ms (vol au moteur, soleil,
 * alarme vibratoire de la chaussette 1 environ toutes les 90 s). Utilisé par le mode démo de
 * l'écran, par la passerelle de simulation et par les aperçus. Retourne le nombre de trames.
 */
int kh_demo_frames(int64_t now_ms, khn_frame_t *out, int max);

#ifdef __cplusplus
}
#endif
