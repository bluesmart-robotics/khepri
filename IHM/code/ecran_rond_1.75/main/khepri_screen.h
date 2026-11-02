/*
 * khepri_screen.h — Écran pilote KHÉPRI, LVGL 8.3 sur AMOLED rond 466×466 (CO5300).
 *
 * Même organisation que bsr_screen (télécommande BSR) : pages qui défilent (tileview), thème
 * noir pur (pixel AMOLED éteint), mise à jour périodique à partir d'une structure de données
 * partagée, et rappels vers la couche radio.
 *
 * Pages : PRINCIPALE · AUTONOMIE · VIBRATIONS · SYSTÈME, plus l'alerte vibratoire plein écran et la
 * progression d'une mise à jour reçue de l'écran énergie (seul connecté au Wi-Fi).
 * Navigation : glisser au doigt, ou molette (accéléromètre aujourd'hui, moteur BLDC demain).
 *
 * Ce module n'appelle aucune fonction ESP-IDF : il compile aussi sur PC pour les aperçus.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kh_model.h"
#include "kh_peer.h"
#include "lvgl.h"
#include "kh_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_SCR_W 466
#define KH_SCR_H 466

/* Couleurs : kh_theme.h (communes aux deux écrans) */

typedef enum { KH_PAGE_MOTORS = 0, KH_PAGE_ENERGY, KH_PAGE_VIB, KH_PAGE_SYSTEM, KH_PAGE_COUNT } kh_page_t;


/* Informations locales à l'écran (pas transmises par la radio) */
typedef struct {
    bool bound;             /* appairé à un maître */
    bool binding;           /* recherche d'un maître en cours */
    bool demo;              /* données simulées localement */
    uint8_t master_mac[6];
    uint32_t heartbeat_ok;  /* commandes acceptées par la pile radio */
    int brightness_pct;
    uint8_t espnow_channel;  /* canal attendu par le maître */
    char reset_text[64];     /* cause du dernier redémarrage (page SYSTÈME) */
    float thrust_ref_w;      /* puissance correspondant à 100 % (page principale) */
    float batt_wh;           /* énergie utile batterie pleine (autonomie) */
    float pv_peak_w;         /* puissance solaire crête installée */
    float level_w;           /* puissance électrique du vol en palier */
    kh_peer_status_t peer;   /* écran énergie voisin (liaison automatique) */
    bool upd_rx;             /* mise à jour en cours de réception depuis l'écran énergie */
    int upd_rx_pct;
    char fw[32];             /* version de ce firmware */
} kh_local_t;

typedef struct {
    void (*on_bind)(bool bind);   /* LIER / DÉLIER */
    void (*on_mark)(void);        /* MARQUEUR : repère pilote dans les enregistrements */
    void (*on_demo)(bool enable); /* SIMULATION locale */
    void (*on_ack)(void);         /* acquittement de l'alerte vibratoire */
    void (*on_brightness)(int pct);
} kh_screen_cb_t;

void kh_screen_init(const kh_screen_cb_t *cb);
/* À appeler sous verrou LVGL, ~10 Hz */
void kh_screen_update(const kh_model_t *m, const kh_local_t *loc, int64_t now_ms);
/* Consigne de régime [%] et instant du dernier réglage (0 = pas de réglage en cours) ; appel bon marché,
 * à faire à chaque tour de la boucle d'affichage */
void kh_screen_set_throttle(float pct, int64_t adjust_ms);
/* Molette : un cran (+1 / −1) → page suivante / précédente ; sur l'alerte, un cran l'acquitte */
/* Petit coup vers l'arrière : acquitte l'alerte affichée, sinon page suivante */
void kh_screen_push(void);
bool kh_screen_alarm_visible(void);
/* Bouton PWR (appui court) : acquitte l'alerte, sinon revient à la page MOTEURS */
void kh_screen_button(void);
void kh_screen_show_page(kh_page_t p, bool anim);
kh_page_t kh_screen_page(void);

#ifdef __cplusplus
}
#endif
