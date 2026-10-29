/*
 * khepri_screen.h — Écran pilote KHÉPRI, LVGL 8.3 sur AMOLED rond 466×466 (CO5300).
 *
 * Même organisation que bsr_screen (télécommande BSR) : pages qui défilent (tileview), thème
 * noir pur (pixel AMOLED éteint), mise à jour périodique à partir d'une structure de données
 * partagée, et rappels vers la couche radio.
 *
 * Pages : MOTEURS · SOLAIRE · VIBRATIONS · SYSTÈME, plus l'alerte vibratoire plein écran.
 * Navigation : glisser au doigt, ou molette (accéléromètre aujourd'hui, moteur BLDC demain).
 *
 * Ce module n'appelle aucune fonction ESP-IDF : il compile aussi sur PC pour les aperçus.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kh_model.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_SCR_W 466
#define KH_SCR_H 466

/* ─── Couleurs (reprises du thème bsr_screen) ─── */
#define KH_COL_BG lv_color_hex(0x000000)
#define KH_COL_CARD lv_color_hex(0x22252E)
#define KH_COL_CARD2 lv_color_hex(0x2A2D38)
#define KH_COL_TEXT lv_color_hex(0xF2F4F8)
#define KH_COL_TEXT_DIM lv_color_hex(0x7A8099)
#define KH_COL_TEXT_MID lv_color_hex(0xB0B8CC)
#define KH_COL_GREEN lv_color_hex(0x4ADE80)
#define KH_COL_AMBER lv_color_hex(0xFBB040)
#define KH_COL_RED lv_color_hex(0xF4614A)
#define KH_COL_BLUE lv_color_hex(0x60A5FA)
#define KH_COL_ACCENT lv_color_hex(0x7DD3C8)
#define KH_COL_SUN lv_color_hex(0xFCD34D)

typedef enum { KH_PAGE_MOTORS = 0, KH_PAGE_SOLAR, KH_PAGE_VIB, KH_PAGE_SYSTEM, KH_PAGE_COUNT } kh_page_t;

/* État de la molette, pour l'indicateur sur le pourtour de l'écran */
typedef struct {
    bool valid;      /* l'entrée est utilisable (carte assez inclinée, ou mode inclinaison) */
    bool tilt_mode;  /* carte à plat : on navigue en l'inclinant (manette) */
    float angle_deg; /* position absolue de la molette, 0 = haut */
} kh_knob_view_t;

/* Informations locales à l'écran (pas transmises par la radio) */
typedef struct {
    bool bound;             /* appairé à un maître */
    bool binding;           /* recherche d'un maître en cours */
    bool demo;              /* données simulées localement */
    uint8_t master_mac[6];
    uint32_t heartbeat_ok;  /* commandes acceptées par la pile radio */
    int brightness_pct;
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
void kh_screen_update(const kh_model_t *m, const kh_local_t *loc, const kh_knob_view_t *knob, int64_t now_ms);
/* Molette : un cran (+1 / −1) → page suivante / précédente ; sur l'alerte, un cran l'acquitte */
void kh_screen_knob_step(int steps);
/* Bouton PWR (appui court) : acquitte l'alerte, sinon revient à la page MOTEURS */
void kh_screen_button(void);
void kh_screen_show_page(kh_page_t p, bool anim);
kh_page_t kh_screen_page(void);

#ifdef __cplusplus
}
#endif
