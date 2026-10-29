/*
 * khepri_link.h — côté écran de la liaison ESP-NOW KHÉPRI (voir khepri_espnow.h).
 *
 * Reprend la logique de la télécommande BSR : l'écran est initiateur espnow_ctrl, l'appairage se
 * fait par un bouton (LIER), les commandes partent en mots de 32 bits. En plus, l'écran retient
 * l'adresse MAC du maître qui lui envoie la télémétrie juste après l'appairage (NVS), et ignore
 * ensuite tout autre émetteur.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "khepri_espnow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*khepri_link_rx_cb_t)(const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi);

/* Wi-Fi station + ESP-NOW ; NVS et boucle d'événements doivent être initialisés */
void khepri_link_init(khepri_link_rx_cb_t cb);
/* LIER (recherche d'un maître pendant 15 s) / DÉLIER (oubli du maître) */
void khepri_link_bind(bool bind);
bool khepri_link_bound(void);
bool khepri_link_binding(void);
void khepri_link_master_mac(uint8_t mac[6]);
/* Commande vers le maître (KHN_CMD_*) ; false si non appairé */
bool khepri_link_send_cmd(uint8_t code, uint32_t arg);
/* À appeler ~1 Hz : fin de la recherche, battement de cœur */
void khepri_link_tick(void);

#ifdef __cplusplus
}
#endif
