/*
 * khepri_screen.h — Écran pilote KHÉPRI, LVGL 8.3 sur AMOLED rond 466×466 (CO5300).
 *
 * Même organisation que bsr_screen (télécommande BSR) : pages qui défilent (tileview), thème
 * noir pur (pixel AMOLED éteint), mise à jour périodique à partir d'une structure de données
 * partagée, et rappels vers la couche radio.
 *
 * Pages : PRINCIPALE · AUTONOMIE · VIBRATIONS · SYSTÈME · WI-FI, plus l'alerte vibratoire plein écran.
 * Navigation : glisser au doigt, ou molette (accéléromètre aujourd'hui, moteur BLDC demain).
 *
 * Ce module n'appelle aucune fonction ESP-IDF : il compile aussi sur PC pour les aperçus.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kh_model.h"
#include "kh_ota.h"
#include "kh_wifi.h"
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

typedef enum { KH_PAGE_MOTORS = 0, KH_PAGE_ENERGY, KH_PAGE_VIB, KH_PAGE_SYSTEM, KH_PAGE_WIFI, KH_PAGE_COUNT } kh_page_t;


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
    kh_wifi_status_t wifi;  /* page WI-FI */
    kh_ota_status_t ota;    /* mises à jour depuis GitHub */
} kh_local_t;

typedef struct {
    void (*on_bind)(bool bind);   /* LIER / DÉLIER */
    void (*on_mark)(void);        /* MARQUEUR : repère pilote dans les enregistrements */
    void (*on_demo)(bool enable); /* SIMULATION locale */
    void (*on_ack)(void);         /* acquittement de l'alerte vibratoire */
    void (*on_brightness)(int pct);
    void (*on_wifi_scan)(void);
    void (*on_wifi_connect)(const char *ssid, const char *password);
    void (*on_wifi_forget)(void);
    void (*on_ota_check)(void);
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
/* Aperçus PC uniquement */
void kh_screen_debug_open_password(const char *ssid, const char *text);

#ifdef __cplusplus
}
#endif
