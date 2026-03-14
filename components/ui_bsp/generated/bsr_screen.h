/*
 * bsr_screen.h
 * Écran télécommande robot treuil — LVGL sur ESP32-S3 AMOLED 280×456
 * Adapté de telecommande_robot.html (4 pages swipables via tileview)
 */

#ifndef BSR_SCREEN_H
#define BSR_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ─── Couleurs thème (du HTML CSS) ─── */
#define BSR_COL_BG          lv_color_hex(0x1A1C22)
#define BSR_COL_CARD        lv_color_hex(0x22252E)
#define BSR_COL_CARD2       lv_color_hex(0x2A2D38)
#define BSR_COL_TEXT        lv_color_hex(0xF2F4F8)
#define BSR_COL_TEXT_DIM    lv_color_hex(0x7A8099)
#define BSR_COL_TEXT_MID    lv_color_hex(0xB0B8CC)
#define BSR_COL_GREEN       lv_color_hex(0x4ADE80)
#define BSR_COL_AMBER       lv_color_hex(0xFBB040)
#define BSR_COL_RED         lv_color_hex(0xF4614A)
#define BSR_COL_BLUE        lv_color_hex(0x60A5FA)
#define BSR_COL_ACCENT      lv_color_hex(0x7DD3C8)
#define BSR_COL_ACCENT2     lv_color_hex(0xA5B4FC)

/* ─── Modes de conduite ─── */
typedef enum {
    BSR_MODE_MANUEL = 0,
    BSR_MODE_PRECISION,
    BSR_MODE_BALANCE,
    BSR_MODE_LOCK,
    BSR_MODE_COUNT
} bsr_drive_mode_t;

/* ─── Structure données télécommande (partagée ESP-NOW <-> écran) ─── */
typedef struct {
    /* Drive */
    float    speed_kmh;
    uint8_t  effort_pct;
    bool     armed;
    bsr_drive_mode_t mode;

    /* Batterie */
    uint8_t  batt_pct;
    float    batt_voltage_l;
    float    batt_voltage_r;
    float    batt_current_l;
    float    batt_current_r;

    /* Températures (°C) */
    int8_t   temp_motor_l;
    int8_t   temp_motor_r;
    int8_t   temp_controller;
    int8_t   temp_batt_l;
    int8_t   temp_batt_r;

    /* Radio ESP-NOW */
    int8_t   rssi;
    uint8_t  latency_ms;
    float    pkt_loss_pct;
    bool     connected;

    /* Réglages par mode */
    struct {
        uint8_t vmax_x10;   /* vitesse max x10 (40 = 4.0 km/h) */
        uint8_t accel;       /* 1-5 */
        uint8_t pmax;        /* 20-100% */
        uint8_t dir_sens;    /* 1-5 */
        uint8_t dir_dead;    /* 0-30% */
    } mode_settings[BSR_MODE_COUNT];

    /* Sécurités */
    bool antislip;
    bool reverse_limited;
    bool gyro_auto;

    /* Alarmes */
    bool  alarm_batt;
    bool  alarm_temp;
    bool  alarm_link_lost;
    bool  alarm_odrive_fault;
    char  alarm_text[64];
} bsr_data_t;

/* ─── Structure UI LVGL ─── */
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *tileview;
    lv_obj_t *tile_drive;
    lv_obj_t *tile_settings;
    lv_obj_t *tile_status;
    lv_obj_t *tile_connection;

    /* PAGE 1 : DRIVE */
    struct {
        lv_obj_t *lbl_batt_pct;
        lv_obj_t *lbl_arm_badge;
        lv_obj_t *arc_effort;
        lv_obj_t *lbl_speed_dir;
        lv_obj_t *lbl_speed_val;
        lv_obj_t *lbl_effort_pct;
        lv_obj_t *lbl_mode_badge;
        lv_obj_t *lbl_batt_big;
        lv_obj_t *lbl_batt_range;
        lv_obj_t *bar_batt;
        lv_obj_t *alarm_panel;
        lv_obj_t *lbl_alarm_text;
    } drive;

    /* PAGE 2 : RÉGLAGES */
    struct {
        lv_obj_t *btn_mode[BSR_MODE_COUNT];
        lv_obj_t *lbl_perf_mode;
        lv_obj_t *cont_vmax;
        lv_obj_t *slider_vmax;
        lv_obj_t *lbl_vmax_val;
        lv_obj_t *cont_accel;
        lv_obj_t *slider_accel;
        lv_obj_t *lbl_accel_val;
        lv_obj_t *slider_pmax;
        lv_obj_t *lbl_pmax_val;
        lv_obj_t *cont_direction;
        lv_obj_t *slider_dir_sens;
        lv_obj_t *lbl_dir_sens_val;
        lv_obj_t *slider_dir_dead;
        lv_obj_t *lbl_dir_dead_val;
        lv_obj_t *sw_antislip;
        lv_obj_t *sw_reverse;
        lv_obj_t *sw_gyro;
        lv_obj_t *perf_card;
        lv_obj_t *sec_card;
    } settings;

    /* PAGE 3 : ÉTAT */
    struct {
        lv_obj_t *cont_global;
        lv_obj_t *lbl_global_status;
        lv_obj_t *lbl_batt_l_val;
        lv_obj_t *lbl_batt_r_val;
        lv_obj_t *lbl_temp_val[5];
        lv_obj_t *bar_temp[5];
        lv_obj_t *lbl_event_text;
        lv_obj_t *lbl_event_time;
    } status;

    /* PAGE 4 : CONNEXION */
    struct {
        lv_obj_t *cont_conn;
        lv_obj_t *lbl_conn_name;
        lv_obj_t *lbl_conn_detail;
        lv_obj_t *list_robots;
        lv_obj_t *btn_search;
        lv_obj_t *btn_disconnect;
        lv_obj_t *lbl_rssi;
        lv_obj_t *lbl_latency;
        lv_obj_t *lbl_pkt_loss;
    } connection;
} bsr_ui_t;

/* ─── Variables globales ─── */
extern bsr_ui_t   bsr_ui;
extern bsr_data_t bsr_data;

/* ─── API publique ─── */
void bsr_screen_init(void);
void bsr_screen_update(void);
void bsr_update_drive(void);
void bsr_update_settings(void);
void bsr_update_status(void);
void bsr_update_connection(void);
void bsr_set_mode(bsr_drive_mode_t mode);

/* Callback quand un réglage change sur l'écran (à envoyer via ESP-NOW) */
typedef void (*bsr_settings_cb_t)(bsr_drive_mode_t mode, const bsr_data_t *data);
void bsr_register_settings_changed_cb(bsr_settings_cb_t cb);

/* Callbacks pour bind/unbind depuis les boutons écran */
typedef void (*bsr_bind_action_cb_t)(bool bind); /* true=bind, false=unbind */
void bsr_register_bind_action_cb(bsr_bind_action_cb_t cb);

/* Naviguer vers la page connexion avec retour auto après delay_ms */
void bsr_show_connection_page(int delay_ms);

#ifdef __cplusplus
}
#endif
#endif /* BSR_SCREEN_H */
