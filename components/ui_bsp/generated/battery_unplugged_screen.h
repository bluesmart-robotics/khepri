/*
 * battery_unplugged_screen.h
 * Ecran BMS LiFePO4 11S deconnecte — mode BLE
 * Variante de battery_screen sans la page USB-C
 * et avec seulement 2 sondes temperature (Cellule + BMS)
 *
 * 3 pages swipables via tileview :
 *   Tile 0 : Resume (SOC arc, tension, courant, puissance)
 *   Tile 1 : Tension pack (graph SOC + cap nominale/dynamique)
 *   Tile 2 : Temperatures (Cellule + BMS) + MOSFET
 */

#ifndef BATTERY_UNPLUGGED_SCREEN_H
#define BATTERY_UNPLUGGED_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "battery_screen.h"   /* reutilise bms_data_t, couleurs, BMS_CELL_COUNT, etc. */

/* ── Structure UI LVGL (variante unplugged) ── */
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *tileview;
    lv_obj_t *tile_resume;
    lv_obj_t *tile_voltage;
    lv_obj_t *tile_temp;

    /* Label batterie locale (status bar, remplace "BMS BLE") */
    lv_obj_t *lbl_local_batt;

    /* PAGE 1 : RESUME (identique a battery_screen) */
    struct {
        lv_obj_t *lbl_state;
        lv_obj_t *arc_soc;
        lv_obj_t *lbl_soc;
        lv_obj_t *lbl_soc_unit;
        lv_obj_t *lbl_volt_val;
        lv_obj_t *lbl_curr_val;
        lv_obj_t *lbl_pow_val;
    } resume;

    /* PAGE 2 : TENSION PACK (identique a battery_screen) */
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

    /* PAGE 3 : TEMPERATURES (Cellule + BMS) + MOS */
    struct {
        lv_obj_t *bar_temp[2];          /* 0=Cellule, 1=BMS */
        lv_obj_t *lbl_temp_val[2];
        lv_obj_t *cont_mos_chg;
        lv_obj_t *cont_mos_dsc;
        lv_obj_t *lbl_mos_chg_state;
        lv_obj_t *lbl_mos_dsc_state;
    } temp;
} bat_unp_ui_t;

/* ── Variables globales ── */
extern bat_unp_ui_t bat_unp_ui;
/* bms_data est deja declare dans battery_screen.h */

/* ── API publique ── */
void bat_unp_screen_init(void);           /* layout paysage 456x280 */
void bat_unp_screen_init_portrait(void);  /* layout portrait 280x456 */
void bat_unp_screen_cleanup(void);
void bat_unp_screen_update(void);

#ifdef __cplusplus
}
#endif
#endif /* BATTERY_UNPLUGGED_SCREEN_H */
