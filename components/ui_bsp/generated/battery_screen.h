/*
 * battery_screen.h
 * Ecran BMS LiFePO4 11S — LVGL sur ESP32-S3 AMOLED 280x456 portrait
 * Adapte de bms_lifepo4_monitor.html
 *
 * 4 pages swipables via tileview :
 *   Tile 0 : Resume (SOC arc, tension, courant, puissance, temp)
 *   Tile 1 : USB-C (2 ports cliquables + chargeur BQ25756)
 *   Tile 2 : Tension pack (graph SOC + cap nominale/dynamique)
 *   Tile 3 : Temperatures + MOSFET
 *
 * + overlay Port Detail (PDO selector, slider courant, puissance)
 */

#ifndef BATTERY_SCREEN_H
#define BATTERY_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Couleurs theme (identiques au HTML) ── */
#define BAT_COL_BG        lv_color_hex(0x000000)  /* noir pur = pixel AMOLED eteint */
#define BAT_COL_CARD      lv_color_hex(0x1A1D27)
#define BAT_COL_CARD2     lv_color_hex(0x22263A)
#define BAT_COL_BORDER    lv_color_hex(0x2E3140)
#define BAT_COL_TEXT      lv_color_hex(0xF2F4F8)
#define BAT_COL_TEXT_DIM  lv_color_hex(0x7A8099)
#define BAT_COL_TEXT_MID  lv_color_hex(0xB0B8CC)
#define BAT_COL_GREEN     lv_color_hex(0x4ADE80)
#define BAT_COL_AMBER     lv_color_hex(0xFBB040)
#define BAT_COL_RED       lv_color_hex(0xF4614A)
#define BAT_COL_BLUE      lv_color_hex(0x60A5FA)
#define BAT_COL_ACCENT    lv_color_hex(0x7DD3C8)
#define BAT_COL_ACCENT2   lv_color_hex(0xA5B4FC)

#define BMS_CELL_COUNT    11
#define SOC_GRAPH_POINTS  40   /* nb points dans le graph SOC */

/* ── Etat BMS ── */
typedef enum {
    BMS_STATE_CHARGE = 0,
    BMS_STATE_DISCHARGE,
    BMS_STATE_IDLE,
    BMS_STATE_ERROR,
} bms_state_t;

/* ── Donnees BMS (partagees ESP-NOW <-> ecran) ── */
typedef struct {
    bms_state_t state;
    uint8_t  soc;                       /* 0-100% */
    float    voltage;                   /* tension pack totale V */
    float    current;                   /* A (+charge, -decharge) */
    float    power;                     /* W */

    /* Temperatures */
    float    temp_cell;                 /* cellules °C */
    float    temp_bms;                  /* BMS interne °C */
    float    temp_usbc;                 /* USB-C °C */

    /* Cellules */
    uint16_t cells_mv[BMS_CELL_COUNT];  /* mV par cellule */
    float    cell_avg;                  /* V moyenne */
    uint16_t cell_delta;                /* mV delta max-min */

    /* USB-C ports */
    struct {
        bool  connected;
        bool  is_sink;                  /* true=sink, false=source */
        float voltage;
        float current;
        float power;
        float contract_v;               /* tension PDO negociee */
        float contract_i;               /* courant alloue */
    } usb_port[2];

    /* Chargeur BQ25756 */
    float    ibat;
    float    charger_temp;
    char     charge_phase[24];

    /* MOSFET */
    bool     mos_chg;
    bool     mos_dsc;

    /* Fan */
    uint8_t  fan_pct;

    /* Capacite */
    uint32_t cap_nominal;               /* mAh */
    uint32_t cap_dynamic;               /* mAh */
    uint32_t cap_origin;                /* mAh (neuve) */
    uint16_t cycles;
} bms_data_t;

/* ── Structure UI LVGL ── */
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *tileview;
    lv_obj_t *tile_resume;
    lv_obj_t *tile_usbc;
    lv_obj_t *tile_voltage;
    lv_obj_t *tile_temp;

    /* PAGE 1 : RESUME */
    struct {
        lv_obj_t *lbl_state;
        lv_obj_t *arc_soc;
        lv_obj_t *lbl_soc;
        lv_obj_t *lbl_soc_unit;
        lv_obj_t *lbl_volt_val;
        lv_obj_t *lbl_curr_val;
        lv_obj_t *lbl_pow_val;
    } resume;

    /* PAGE 2 : USB-C */
    struct {
        lv_obj_t *cont_main;           /* vue principale (ports) */
        lv_obj_t *port_block[2];       /* blocs cliquables */
        lv_obj_t *lbl_p_role[2];
        lv_obj_t *lbl_p_v[2];
        lv_obj_t *lbl_p_a[2];
        lv_obj_t *lbl_p_w[2];
        lv_obj_t *lbl_chg_phase;
        lv_obj_t *lbl_chg_ibat;
        lv_obj_t *lbl_chg_temp;
        /* Port detail overlay */
        lv_obj_t *cont_detail;
        lv_obj_t *lbl_detail_title;
        lv_obj_t *lbl_detail_badge;
        lv_obj_t *slider_current;
        lv_obj_t *lbl_slider_val;
        lv_obj_t *lbl_pwr_p1;
        lv_obj_t *lbl_pwr_p2;
        lv_obj_t *lbl_pwr_total;
        lv_obj_t *pdo_btns[7];
        int       detail_port_idx;     /* 0 ou 1 */
    } usbc;

    /* PAGE 3 : TENSION PACK */
    struct {
        lv_obj_t *lbl_total;
        lv_obj_t *lbl_avg;
        lv_obj_t *lbl_delta;
        lv_obj_t *chart;
        lv_chart_series_t *ser_soc;
        lv_obj_t *lbl_cap_nom_val;
        lv_obj_t *lbl_cap_nom_sub;
        lv_obj_t *lbl_cap_dyn_val;
        lv_obj_t *lbl_cap_dyn_sub;
    } volt;

    /* PAGE 4 : TEMPERATURES + MOS */
    struct {
        lv_obj_t *bar_temp[3];
        lv_obj_t *lbl_temp_val[3];
        lv_obj_t *slider_fan;
        lv_obj_t *lbl_fan_val;
        lv_obj_t *cont_mos_chg;
        lv_obj_t *cont_mos_dsc;
        lv_obj_t *lbl_mos_chg_state;
        lv_obj_t *lbl_mos_dsc_state;
    } temp;
} battery_ui_t;

/* ── Variables globales ── */
extern battery_ui_t battery_ui;
extern bms_data_t   bms_data;

/* ── API publique ── */
void battery_screen_init(void);
void battery_screen_cleanup(void);
void battery_screen_update(void);

#ifdef __cplusplus
}
#endif
#endif /* BATTERY_SCREEN_H */
