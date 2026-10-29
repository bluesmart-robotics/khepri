/*
 * khepri_espnow.h — KHÉPRI : liaison radio ESP-NOW entre le maître (fuselage) et l'écran pilote.
 *
 * Même principe que la télécommande BSR (bsr_espnow_control.c), avec la bibliothèque
 * espressif/esp-now :
 *
 *  1. Appairage par espnow_ctrl : l'écran est « initiateur » (bouton LIER), le maître est
 *     « répondeur » (espnow_ctrl_responder_bind). Attribut initiateur : KHN_ATTR_INITIATOR,
 *     différent de celui de la télécommande BSR (KEY_1) pour qu'un écran KHEPRI ne se lie
 *     jamais à un robot BSR, et inversement.
 *
 *  2. Commandes écran → maître : un mot de 32 bits envoyé par espnow_ctrl_initiator_send(),
 *     code dans le quartet de poids fort, exactement comme les touches de la télécommande BSR.
 *
 *  3. Télémétrie maître → écran : paquets ESPNOW_DATA_TYPE_DATA qui transportent tels quels
 *     les trames CAN utiles à l'écran (SYNC, trames des chaussettes, régime du BSR Drive).
 *     L'écran décode donc exactement les mêmes octets que le maître sur le bus CAN.
 *
 * Fichier sans dépendance ESP-IDF : utilisé par l'écran, par la passerelle de simulation
 * et par les tests sur PC.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHN_VERSION 1
#define KHN_MAGIC0 'K'
#define KHN_MAGIC1 'H'

/* Attributs espnow_ctrl (valeurs de espnow_attribute_t, recopiées pour rester sans dépendance) */
#define KHN_ATTR_INITIATOR 0x0207u /* ESPNOW_ATTRIBUTE_KEY_7 : écran pilote KHEPRI */
#define KHN_ATTR_RESPONDER 0x0003u /* ESPNOW_ATTRIBUTE_ATTRIBUTE                   */

/* Taille utile maximale d'un paquet ESP-NOW de la bibliothèque (ESPNOW_DATA_LEN, sans chiffrement) */
#define KHN_PAYLOAD_MAX 230

/* ------------------------------------------------------------------ télémétrie maître → écran */
enum {
    KHN_T_CAN_BATCH = 1, /* lot de trames CAN */
};
enum {
    KHN_HF_DEMO = 1u << 0, /* données simulées (passerelle de test) : l'écran affiche « SIMULATION » */
};

/* En-tête : 8 octets */
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t seq;   /* incrémenté à chaque paquet : sert à compter les pertes */
    uint8_t count;  /* nombre de trames */
    uint8_t flags;  /* KHN_HF_* */
} khn_hdr_t;
#define KHN_HDR_LEN 8

/* Trame CAN transportée : id (u16, 11 bits), dlc (u8), puis dlc octets de données */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} khn_frame_t;
#define KHN_FRAME_MAX_LEN 11
#define KHN_MAX_FRAMES ((KHN_PAYLOAD_MAX - KHN_HDR_LEN) / KHN_FRAME_MAX_LEN) /* 20 */

/* Écrit l'en-tête puis les trames ; retourne la taille écrite, 0 si cap est trop petit. */
size_t khn_pack(uint8_t *buf, size_t cap, uint16_t seq, uint8_t flags, const khn_frame_t *f, size_t n);
/* Vérifie et décode un paquet ; retourne le nombre de trames, −1 si le paquet est invalide. */
int khn_unpack(const uint8_t *buf, size_t len, khn_hdr_t *hdr, khn_frame_t *out, size_t max);

/* ------------------------------------------------------------------ commandes écran → maître */
enum {
    KHN_CMD_ACK_ALARM = 0x1, /* arg : chaussette (bits 0-1) — le pilote a vu l'alarme       */
    KHN_CMD_MARK = 0x2,      /* arg : code de marqueur (16 bits) → « mark » sur les nœuds  */
    KHN_CMD_LOG = 0x3,       /* arg : 1 démarre, 0 arrête l'enregistrement des chaussettes */
    KHN_CMD_HEARTBEAT = 0x4, /* arg : compteur (16 bits), 1 Hz — l'écran est vivant         */
    KHN_CMD_THROTTLE = 0x5,  /* arg : consigne de régime en ‰ de la puissance de référence   */
                             /*       (0 à 1220) ; le maître décide s'il l'applique          */
};
static inline uint32_t khn_cmd(uint8_t code, uint32_t arg) { return ((uint32_t)(code & 0xFu) << 28) | (arg & 0x0FFFFFFFu); }
static inline uint8_t khn_cmd_code(uint32_t v) { return (uint8_t)(v >> 28); }
static inline uint32_t khn_cmd_arg(uint32_t v) { return v & 0x0FFFFFFFu; }

#ifdef __cplusplus
}
#endif
