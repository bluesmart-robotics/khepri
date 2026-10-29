/*
 * khepri_relay.h — côté maître de la liaison ESP-NOW vers l'écran pilote.
 *
 * À intégrer tel quel dans le firmware du maître (khepri-chaussette-fw/master) :
 *
 *   khepri_relay_init();                       // après esp_wifi_start(), sur le canal de l'écran
 *   khepri_relay_push(id, data, dlc);          // pour CHAQUE trame CAN reçue (mcan.c, tâche RX)
 *   khepri_relay_push(KP_ID_SYNC, sync, 8);    // et pour la trame SYNC émise par le maître
 *
 * Le relais ne garde que les trames utiles à l'écran (SYNC, trames des chaussettes 0x601-0x63F
 * hors transfert de fichiers, Get_Encoder_Estimates des deux axes du BSR Drive), conserve la
 * dernière valeur de chaque identifiant et les envoie à 10 Hz, en un seul paquet ESP-NOW, à
 * chaque écran appairé. Les alarmes vibratoires ne sont jamais fusionnées.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "khepri_espnow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*khepri_relay_cmd_cb_t)(uint8_t code, uint32_t arg);

typedef struct {
    uint8_t odrv_node_m0, odrv_node_m1; /* nœuds CANSimple des axes du BSR Drive */
    int8_t bind_rssi;                   /* LIER accepté seulement si l'écran est tout près */
    uint32_t period_ms;                 /* cadence d'envoi */
    uint8_t flags;                      /* KHN_HF_* (KHN_HF_DEMO pour la passerelle de simulation) */
    khepri_relay_cmd_cb_t on_cmd;       /* commandes de l'écran (REPÈRE, acquittement...) */
} khepri_relay_cfg_t;

#define KHEPRI_RELAY_CFG_DEFAULT() \
    { .odrv_node_m0 = 0, .odrv_node_m1 = 1, .bind_rssi = -55, .period_ms = 100, .flags = 0, .on_cmd = NULL }

/* ESP-NOW doit être initialisé (espnow_init) avant l'appel */
esp_err_t khepri_relay_init(const khepri_relay_cfg_t *cfg);
/* Thread-safe ; ne bloque pas */
void khepri_relay_push(uint32_t id, const uint8_t *data, uint8_t dlc);
/* Nombre d'écrans appairés, paquets envoyés */
int khepri_relay_displays(void);
uint32_t khepri_relay_sent(void);

#ifdef __cplusplus
}
#endif
