/*
 * energie_screen.h — écran ÉNERGIE KHÉPRI, LVGL 8.3 sur AMOLED rectangulaire 280×456 (portrait).
 *
 * Rôle : à côté de l'écran rond (régime moteur), il montre en permanence l'énergie, sans
 * changer de page en vol :
 *   - au centre, la jauge de batterie (état de charge), le pourcentage toujours loin du bord de
 *     remplissage (en bas au-dessus de 50 %, en haut en dessous) ;
 *   - à gauche, un fin trait jaune : la production solaire (vide = rien, plein = crête installée) ;
 *   - à droite, le bilan : une flèche qui part du milieu, vers le haut quand la batterie se charge,
 *     vers le bas quand elle se vide, d'autant plus longue que le flux est fort.
 * Glisser le doigt vers la droite : DÉTAILS (valeurs en W, V, A, Wh, Ah), puis RÉGLAGES (liaison,
 * simulation, luminosité, Wi-Fi, mises à jour). Retour automatique à l'énergie après 10 s sans
 * toucher l'écran.
 *
 * Ce module n'appelle aucune fonction ESP-IDF : il compile aussi sur PC pour les aperçus.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kh_model.h"
#include "kh_ota.h"
#include "kh_theme.h"
#include "kh_wifi.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KE_SCR_W 280
#define KE_SCR_H 456
#define KE_RETURN_MS 10000 /* retour à la page énergie sans toucher l'écran */

typedef enum { KE_PAGE_SETTINGS = 0, KE_PAGE_DETAILS, KE_PAGE_ENERGY, KE_PAGE_COUNT } ke_page_t;

/* Informations locales à l'écran (pas transmises par la radio) */
typedef struct {
    bool bound, binding, demo;
    uint8_t master_mac[6];
    int brightness_pct;
    uint8_t espnow_channel;
    float thrust_ref_w, batt_wh, pv_peak_w, level_w;
    char board[8]; /* révision de carte : « V1 » ou « V1.1 » */
    kh_wifi_status_t wifi;
    kh_ota_status_t ota;
} ke_local_t;

typedef struct {
    void (*on_bind)(bool bind);
    void (*on_demo)(bool en);
    void (*on_brightness)(int pct);
    void (*on_wifi_scan)(void);
    void (*on_wifi_connect)(const char *ssid, const char *password);
    void (*on_wifi_forget)(void);
    void (*on_ota_check)(void);
} ke_cb_t;

void ke_screen_init(const ke_cb_t *cb);
/* À appeler par la seule tâche LVGL, à chaque nouvel instantané (et au moins toutes les 250 ms) */
void ke_screen_update(const kh_model_t *m, const ke_local_t *loc, int64_t now_ms);
void ke_screen_show_page(ke_page_t p, bool anim);
ke_page_t ke_screen_page(void);

/* Aperçus PC */
void ke_screen_debug_wifi(bool open);
void ke_screen_debug_password(const char *ssid, const char *text);

#ifdef __cplusplus
}
#endif
