#include "esp_log.h"
/*
 * bsr_screen.c
 * Ecran telecommande robot treuil - LVGL sur ESP32-S3 AMOLED 280x456
 * Adapte de telecommande_robot.html
 *
 * Architecture : tileview horizontal (4 tuiles = 4 pages)
 *   Tile 0 : Drive (vitesse, effort arc, batterie, alarmes)
 *   Tile 1 : Reglages (mode, sliders, toggles)
 *   Tile 2 : Etat robot (temperatures, batteries, encodeurs)
 *   Tile 3 : Connexion (ESP-NOW, robots connus, RSSI)
 */

#include "bsr_screen.h"
#include <stdio.h>
#include <string.h>
#include <math.h>


/* --- Instances globales --- */
bsr_ui_t   bsr_ui;
bsr_data_t bsr_data;

static bsr_settings_cb_t s_settings_cb = NULL;
static bsr_bind_action_cb_t s_bind_action_cb = NULL;

/* --- Constantes --- */
#define SCREEN_W  280
#define SCREEN_H  456

static const char *mode_names[BSR_MODE_COUNT]  = {"MANUEL", "PRECISION", "SELF-BALANCE", "LOCK"};
static const char *accel_labels[5] = {"Tres douce", "Douce", "Normale", "Reactive", "Brusque"};
static const char *dir_sens_labels[5] = {"Tres douce", "Douce", "Normale", "Vive", "Tres vive"};
static const char *temp_names[5] = {"Mot. G", "Mot. D", "Ctrl", "Batt. G", "Batt. D"};

/* --- Prototypes internes --- */
static void create_page_drive(lv_obj_t *parent);
static void create_page_settings(lv_obj_t *parent);
static void create_page_status(lv_obj_t *parent);
static void create_page_connection(lv_obj_t *parent);
static void notify_settings_changed(void);

/* --- Helpers style --- */
static void set_card_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, BSR_COL_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2E3140), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color,
                            const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

/* ====================================================
 *  INIT PRINCIPAL
 * ==================================================== */
void bsr_screen_init(void)
{

    /* Valeurs par defaut */
    memset(&bsr_data, 0, sizeof(bsr_data));
    bsr_data.armed = true;
    bsr_data.mode = BSR_MODE_BALANCE;
    bsr_data.batt_pct = 78;
    bsr_data.connected = false;
    bsr_data.rssi = 0;
    bsr_data.latency_ms = 0;
    bsr_data.antislip = true;
    bsr_data.reverse_limited = true;
    bsr_data.gyro_auto = true;
    bsr_data.batt_voltage_l = 36.8f;
    bsr_data.batt_voltage_r = 36.6f;
    bsr_data.batt_current_l = 12.0f;
    bsr_data.batt_current_r = 11.0f;
    bsr_data.temp_motor_l = 44;
    bsr_data.temp_motor_r = 43;
    bsr_data.temp_controller = 38;
    bsr_data.temp_batt_l = 31;
    bsr_data.temp_batt_r = 32;


    /* Reglages par defaut par mode (vmax_x10, accel, pmax, dir_sens, dir_dead) */
    bsr_data.mode_settings[BSR_MODE_MANUEL].vmax_x10 = 50;
    bsr_data.mode_settings[BSR_MODE_MANUEL].accel = 3;
    bsr_data.mode_settings[BSR_MODE_MANUEL].pmax = 80;
    bsr_data.mode_settings[BSR_MODE_MANUEL].dir_sens = 3;
    bsr_data.mode_settings[BSR_MODE_MANUEL].dir_dead = 10;

    bsr_data.mode_settings[BSR_MODE_PRECISION].vmax_x10 = 25;
    bsr_data.mode_settings[BSR_MODE_PRECISION].accel = 2;
    bsr_data.mode_settings[BSR_MODE_PRECISION].pmax = 60;
    bsr_data.mode_settings[BSR_MODE_PRECISION].dir_sens = 2;
    bsr_data.mode_settings[BSR_MODE_PRECISION].dir_dead = 15;

    bsr_data.mode_settings[BSR_MODE_BALANCE].vmax_x10 = 35;
    bsr_data.mode_settings[BSR_MODE_BALANCE].accel = 2;
    bsr_data.mode_settings[BSR_MODE_BALANCE].pmax = 70;
    bsr_data.mode_settings[BSR_MODE_BALANCE].dir_sens = 3;
    bsr_data.mode_settings[BSR_MODE_BALANCE].dir_dead = 10;

    bsr_data.mode_settings[BSR_MODE_LOCK].vmax_x10 = 10;
    bsr_data.mode_settings[BSR_MODE_LOCK].accel = 1;
    bsr_data.mode_settings[BSR_MODE_LOCK].pmax = 50;
    bsr_data.mode_settings[BSR_MODE_LOCK].dir_sens = 3;
    bsr_data.mode_settings[BSR_MODE_LOCK].dir_dead = 10;


    /* Theme dark mode - evite la recoloration rouge sur press */
    lv_theme_t *th = lv_theme_default_init(lv_disp_get_default(),
                                           BSR_COL_ACCENT, BSR_COL_ACCENT2,
                                           true, &lv_font_montserrat_20);
    lv_disp_set_theme(lv_disp_get_default(), th);

    /* Creer l'ecran */
    bsr_ui.screen = lv_obj_create(NULL);
    lv_obj_set_size(bsr_ui.screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(bsr_ui.screen, BSR_COL_BG, 0);
    lv_obj_set_style_bg_opa(bsr_ui.screen, LV_OPA_COVER, 0);

    /* Tileview horizontal (swipe entre pages) */
    bsr_ui.tileview = lv_tileview_create(bsr_ui.screen);
    lv_obj_set_size(bsr_ui.tileview, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(bsr_ui.tileview, 0, 0);
    lv_obj_set_style_bg_color(bsr_ui.tileview, BSR_COL_BG, 0);
    lv_obj_set_scrollbar_mode(bsr_ui.tileview, LV_SCROLLBAR_MODE_OFF);


    bsr_ui.tile_drive      = lv_tileview_add_tile(bsr_ui.tileview, 0, 0, LV_DIR_RIGHT);
    bsr_ui.tile_settings   = lv_tileview_add_tile(bsr_ui.tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    bsr_ui.tile_status     = lv_tileview_add_tile(bsr_ui.tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    bsr_ui.tile_connection = lv_tileview_add_tile(bsr_ui.tileview, 3, 0, LV_DIR_LEFT);

    ESP_LOGI("BSR", ">>> create_page_drive"); create_page_drive(bsr_ui.tile_drive); ESP_LOGI("BSR", "<<< create_page_drive OK");
    ESP_LOGI("BSR", ">>> create_page_settings"); create_page_settings(bsr_ui.tile_settings); ESP_LOGI("BSR", "<<< create_page_settings OK");
    ESP_LOGI("BSR", ">>> create_page_status"); create_page_status(bsr_ui.tile_status); ESP_LOGI("BSR", "<<< create_page_status OK");
    ESP_LOGI("BSR", ">>> create_page_connection"); create_page_connection(bsr_ui.tile_connection); ESP_LOGI("BSR", "<<< create_page_connection OK");

    lv_scr_load(bsr_ui.screen);
    bsr_screen_update();
}

/* ====================================================
 *  PAGE 1 : DRIVE
 * ==================================================== */
static void create_page_drive(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, BSR_COL_BG, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Barre de statut */
    lv_obj_t *bar_top = lv_obj_create(parent);
    lv_obj_set_size(bar_top, SCREEN_W, 36);
    lv_obj_set_pos(bar_top, 0, 0);
    lv_obj_set_style_bg_color(bar_top, BSR_COL_BG, 0);
    lv_obj_set_style_bg_opa(bar_top, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bar_top, 0, 0);
    lv_obj_set_style_pad_all(bar_top, 0, 0);
    lv_obj_set_scrollbar_mode(bar_top, LV_SCROLLBAR_MODE_OFF);

    bsr_ui.drive.lbl_batt_pct = make_label(bar_top, "78%", BSR_COL_GREEN,
                                            &lv_font_montserrat_20);
    lv_obj_set_pos(bsr_ui.drive.lbl_batt_pct, 12, 10);

    bsr_ui.drive.lbl_arm_badge = lv_label_create(bar_top);
    lv_label_set_text(bsr_ui.drive.lbl_arm_badge, "ARME");
    lv_obj_set_style_text_color(bsr_ui.drive.lbl_arm_badge, BSR_COL_GREEN, 0);
    lv_obj_set_style_text_font(bsr_ui.drive.lbl_arm_badge, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(bsr_ui.drive.lbl_arm_badge, lv_color_hex(0x0D3D1F), 0);
    lv_obj_set_style_bg_opa(bsr_ui.drive.lbl_arm_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bsr_ui.drive.lbl_arm_badge, 8, 0);
    lv_obj_set_style_pad_ver(bsr_ui.drive.lbl_arm_badge, 2, 0);
    lv_obj_set_style_radius(bsr_ui.drive.lbl_arm_badge, 4, 0);
    lv_obj_align(bsr_ui.drive.lbl_arm_badge, LV_ALIGN_TOP_MID, 0, 6);

    /* Arc effort moteur */
    bsr_ui.drive.arc_effort = lv_arc_create(parent);
    lv_obj_set_size(bsr_ui.drive.arc_effort, 220, 220);
    lv_obj_align(bsr_ui.drive.arc_effort, LV_ALIGN_TOP_MID, 0, 50);
    lv_arc_set_range(bsr_ui.drive.arc_effort, 0, 100);
    lv_arc_set_value(bsr_ui.drive.arc_effort, 0);
    lv_arc_set_bg_angles(bsr_ui.drive.arc_effort, 135, 45);
    lv_obj_remove_style(bsr_ui.drive.arc_effort, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(bsr_ui.drive.arc_effort, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(bsr_ui.drive.arc_effort, BSR_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bsr_ui.drive.arc_effort, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(bsr_ui.drive.arc_effort, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(bsr_ui.drive.arc_effort, BSR_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(bsr_ui.drive.arc_effort, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(bsr_ui.drive.arc_effort, true, LV_PART_INDICATOR);

    /* Labels vitesse au centre */
    bsr_ui.drive.lbl_speed_dir = make_label(parent, "-- ARRET --", BSR_COL_TEXT_DIM,
                                             &lv_font_montserrat_18);
    lv_obj_align(bsr_ui.drive.lbl_speed_dir, LV_ALIGN_TOP_MID, 0, 110);

    bsr_ui.drive.lbl_speed_val = make_label(parent, "0.0", BSR_COL_TEXT,
                                             &lv_font_montserrat_48);
    lv_obj_align(bsr_ui.drive.lbl_speed_val, LV_ALIGN_TOP_MID, 0, 125);

    make_label(parent, "km/h", BSR_COL_TEXT_DIM, &lv_font_montserrat_20);
    lv_obj_t *u = lv_obj_get_child(parent, -1);
    lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 178);

    bsr_ui.drive.lbl_effort_pct = make_label(parent, "0%", BSR_COL_ACCENT,
                                              &lv_font_montserrat_20);
    lv_obj_align(bsr_ui.drive.lbl_effort_pct, LV_ALIGN_TOP_MID, 0, 195);

    /* Badge mode */
    bsr_ui.drive.lbl_mode_badge = lv_label_create(parent);
    lv_label_set_text(bsr_ui.drive.lbl_mode_badge, "SELF-BALANCE");
    lv_obj_set_style_text_color(bsr_ui.drive.lbl_mode_badge, BSR_COL_ACCENT2, 0);
    lv_obj_set_style_text_font(bsr_ui.drive.lbl_mode_badge, &lv_font_montserrat_22, 0);
    lv_obj_set_style_bg_color(bsr_ui.drive.lbl_mode_badge, lv_color_hex(0x252840), 0);
    lv_obj_set_style_bg_opa(bsr_ui.drive.lbl_mode_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bsr_ui.drive.lbl_mode_badge, 16, 0);
    lv_obj_set_style_pad_ver(bsr_ui.drive.lbl_mode_badge, 6, 0);
    lv_obj_set_style_radius(bsr_ui.drive.lbl_mode_badge, 8, 0);
    lv_obj_align(bsr_ui.drive.lbl_mode_badge, LV_ALIGN_TOP_MID, 0, 280);

    /* Carte batterie */
    lv_obj_t *batt_card = lv_obj_create(parent);
    lv_obj_set_size(batt_card, SCREEN_W - 24, 95);
    lv_obj_set_pos(batt_card, 12, 320);
    set_card_style(batt_card);
    lv_obj_set_scrollbar_mode(batt_card, LV_SCROLLBAR_MODE_OFF);

    bsr_ui.drive.lbl_batt_big = make_label(batt_card, "78%", BSR_COL_GREEN,
                                            &lv_font_montserrat_28);
    lv_obj_set_pos(bsr_ui.drive.lbl_batt_big, 0, 0);

    bsr_ui.drive.lbl_batt_range = make_label(batt_card, "~ 4.2 km", BSR_COL_TEXT_MID,
                                              &lv_font_montserrat_24);
    lv_obj_align(bsr_ui.drive.lbl_batt_range, LV_ALIGN_TOP_RIGHT, 0, 6);

    bsr_ui.drive.bar_batt = lv_bar_create(batt_card);
    lv_obj_set_size(bsr_ui.drive.bar_batt, SCREEN_W - 48, 7);
    lv_obj_set_pos(bsr_ui.drive.bar_batt, 0, 40);
    lv_bar_set_range(bsr_ui.drive.bar_batt, 0, 100);
    lv_bar_set_value(bsr_ui.drive.bar_batt, 78, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bsr_ui.drive.bar_batt, BSR_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bsr_ui.drive.bar_batt, BSR_COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bsr_ui.drive.bar_batt, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bsr_ui.drive.bar_batt, 4, LV_PART_INDICATOR);

    lv_obj_t *sub = make_label(batt_card, "Autonomie estimee", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Panneau alarmes */
    bsr_ui.drive.alarm_panel = lv_obj_create(parent);
    lv_obj_set_size(bsr_ui.drive.alarm_panel, SCREEN_W, 36);
    lv_obj_align(bsr_ui.drive.alarm_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bsr_ui.drive.alarm_panel, BSR_COL_RED, 0);
    lv_obj_set_style_bg_opa(bsr_ui.drive.alarm_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bsr_ui.drive.alarm_panel, 0, 0);
    lv_obj_set_style_radius(bsr_ui.drive.alarm_panel, 0, 0);
    lv_obj_set_style_pad_left(bsr_ui.drive.alarm_panel, 12, 0);
    lv_obj_set_scrollbar_mode(bsr_ui.drive.alarm_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(bsr_ui.drive.alarm_panel, LV_OBJ_FLAG_HIDDEN);

    bsr_ui.drive.lbl_alarm_text = make_label(bsr_ui.drive.alarm_panel, "",
                                              lv_color_white(), &lv_font_montserrat_18);
    lv_obj_align(bsr_ui.drive.lbl_alarm_text, LV_ALIGN_LEFT_MID, 0, 0);
}

/* ====================================================
 *  PAGE 2 : REGLAGES — callbacks
 * ==================================================== */
static void slider_vmax_cb(lv_event_t *e) {
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    bsr_data.mode_settings[bsr_data.mode].vmax_x10 = (uint8_t)val;
    char buf[16]; snprintf(buf, sizeof(buf), "%.1f km/h", val / 10.0f);
    lv_label_set_text(bsr_ui.settings.lbl_vmax_val, buf);
    notify_settings_changed();
}
static void slider_accel_cb(lv_event_t *e) {
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    bsr_data.mode_settings[bsr_data.mode].accel = (uint8_t)val;
    lv_label_set_text(bsr_ui.settings.lbl_accel_val, accel_labels[val - 1]);
    notify_settings_changed();
}
static void slider_pmax_cb(lv_event_t *e) {
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    bsr_data.mode_settings[bsr_data.mode].pmax = (uint8_t)val;
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", (int)val);
    lv_label_set_text(bsr_ui.settings.lbl_pmax_val, buf);
    notify_settings_changed();
}
static void slider_dir_sens_cb(lv_event_t *e) {
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    bsr_data.mode_settings[bsr_data.mode].dir_sens = (uint8_t)val;
    lv_label_set_text(bsr_ui.settings.lbl_dir_sens_val, dir_sens_labels[val - 1]);
    notify_settings_changed();
}
static void slider_dir_dead_cb(lv_event_t *e) {
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    bsr_data.mode_settings[bsr_data.mode].dir_dead = (uint8_t)val;
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", (int)val);
    lv_label_set_text(bsr_ui.settings.lbl_dir_dead_val, buf);
    notify_settings_changed();
}
static void mode_btn_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    for (int i = 0; i < BSR_MODE_COUNT; i++) {
        if (bsr_ui.settings.btn_mode[i] == btn) {
            bsr_set_mode((bsr_drive_mode_t)i);
            break;
        }
    }
}
static void switch_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (sw == bsr_ui.settings.sw_antislip)  bsr_data.antislip = on;
    if (sw == bsr_ui.settings.sw_reverse)   bsr_data.reverse_limited = on;
    if (sw == bsr_ui.settings.sw_gyro)      bsr_data.gyro_auto = on;
    notify_settings_changed();
}

/* Helper : slider row */
static lv_obj_t *make_slider_row(lv_obj_t *parent, const char *label_text,
                                  int32_t mn, int32_t mx, int32_t val,
                                  lv_obj_t **out_slider, lv_obj_t **out_lbl,
                                  lv_event_cb_t cb)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    make_label(row, label_text, BSR_COL_TEXT_MID, &lv_font_montserrat_20);
    *out_lbl = make_label(row, "", BSR_COL_TEXT, &lv_font_montserrat_20);
    lv_obj_align(*out_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    *out_slider = lv_slider_create(cont);
    lv_obj_set_width(*out_slider, lv_pct(100));
    lv_obj_set_height(*out_slider, 8);
    lv_slider_set_range(*out_slider, mn, mx);
    lv_slider_set_value(*out_slider, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(*out_slider, BSR_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*out_slider, BSR_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*out_slider, BSR_COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(*out_slider, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(*out_slider, cb, LV_EVENT_VALUE_CHANGED, NULL);

    return cont;
}

/* Helper : switch row */
static lv_obj_t *make_switch_row(lv_obj_t *parent, const char *label_text,
                                  bool initial, lv_obj_t **out_sw)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    make_label(row, label_text, BSR_COL_TEXT_MID, &lv_font_montserrat_20);
    *out_sw = lv_switch_create(row);
    lv_obj_align(*out_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_size(*out_sw, 36, 20);
    if (initial) lv_obj_add_state(*out_sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(*out_sw, BSR_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*out_sw, BSR_COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(*out_sw, switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

static void create_page_settings(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, BSR_COL_BG, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, SCREEN_W, 42);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, BSR_COL_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *title = make_label(hdr, "Reglages", BSR_COL_TEXT, &lv_font_montserrat_22);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Scroll */
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, SCREEN_W, SCREEN_H - 42);
    lv_obj_set_pos(scroll, 0, 42);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 10, 0);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);

    /* Mode buttons 2x2 */
    make_label(scroll, "MODE DE CONDUITE", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);

    lv_obj_t *grid = lv_obj_create(scroll);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, 140);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    const char *btn_labels[BSR_MODE_COUNT] = {"Manuel", "Precision", "Balance", "Lock"};
    for (int i = 0; i < BSR_MODE_COUNT; i++) {
        bsr_ui.settings.btn_mode[i] = lv_btn_create(grid);
        lv_obj_set_size(bsr_ui.settings.btn_mode[i], 120, 60);
        lv_obj_set_pos(bsr_ui.settings.btn_mode[i], (i % 2) * 128, (i / 2) * 68);
        lv_obj_set_style_bg_color(bsr_ui.settings.btn_mode[i], BSR_COL_CARD, 0);
        lv_obj_set_style_radius(bsr_ui.settings.btn_mode[i], 12, 0);
        lv_obj_set_style_border_width(bsr_ui.settings.btn_mode[i], 1, 0);
        lv_obj_set_style_border_color(bsr_ui.settings.btn_mode[i], lv_color_hex(0x2E3140), 0);
        lv_obj_add_event_cb(bsr_ui.settings.btn_mode[i], mode_btn_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = make_label(bsr_ui.settings.btn_mode[i], btn_labels[i],
                                    BSR_COL_TEXT, &lv_font_montserrat_20);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    /* Performances */
    bsr_ui.settings.lbl_perf_mode = make_label(scroll, "PERFORMANCES",
                                                BSR_COL_TEXT_DIM, &lv_font_montserrat_18);

    lv_obj_t *perf = lv_obj_create(scroll);
    bsr_ui.settings.perf_card = perf;
    lv_obj_set_width(perf, lv_pct(100));
    lv_obj_set_height(perf, LV_SIZE_CONTENT);
    set_card_style(perf);
    lv_obj_set_layout(perf, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(perf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(perf, 4, 0);
    lv_obj_set_scrollbar_mode(perf, LV_SCROLLBAR_MODE_OFF);

    bsr_ui.settings.cont_vmax = make_slider_row(perf, "Vitesse max", 5, 60, 35,
        &bsr_ui.settings.slider_vmax, &bsr_ui.settings.lbl_vmax_val, slider_vmax_cb);
    bsr_ui.settings.cont_accel = make_slider_row(perf, "Acceleration", 1, 5, 2,
        &bsr_ui.settings.slider_accel, &bsr_ui.settings.lbl_accel_val, slider_accel_cb);
    make_slider_row(perf, "Puissance max", 20, 100, 70,
        &bsr_ui.settings.slider_pmax, &bsr_ui.settings.lbl_pmax_val, slider_pmax_cb);

    /* Direction */
    make_label(scroll, "DIRECTION", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
    bsr_ui.settings.cont_direction = lv_obj_create(scroll);
    lv_obj_set_width(bsr_ui.settings.cont_direction, lv_pct(100));
    lv_obj_set_height(bsr_ui.settings.cont_direction, LV_SIZE_CONTENT);
    set_card_style(bsr_ui.settings.cont_direction);
    lv_obj_set_layout(bsr_ui.settings.cont_direction, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bsr_ui.settings.cont_direction, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bsr_ui.settings.cont_direction, 4, 0);
    lv_obj_set_scrollbar_mode(bsr_ui.settings.cont_direction, LV_SCROLLBAR_MODE_OFF);

    make_slider_row(bsr_ui.settings.cont_direction, "Sensibilite", 1, 5, 3,
        &bsr_ui.settings.slider_dir_sens, &bsr_ui.settings.lbl_dir_sens_val, slider_dir_sens_cb);
    make_slider_row(bsr_ui.settings.cont_direction, "Zone morte", 0, 30, 10,
        &bsr_ui.settings.slider_dir_dead, &bsr_ui.settings.lbl_dir_dead_val, slider_dir_dead_cb);

    /* Securites */
    make_label(scroll, "SECURITES", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_t *sec = lv_obj_create(scroll);
    bsr_ui.settings.sec_card = sec;
    lv_obj_set_width(sec, lv_pct(100));
    lv_obj_set_height(sec, LV_SIZE_CONTENT);
    set_card_style(sec);
    lv_obj_set_layout(sec, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 6, 0);
    lv_obj_set_scrollbar_mode(sec, LV_SCROLLBAR_MODE_OFF);

    make_switch_row(sec, "Anti-patinage", true, &bsr_ui.settings.sw_antislip);
    make_switch_row(sec, "Arr. limitee", true, &bsr_ui.settings.sw_reverse);
    make_switch_row(sec, "Gyrophare auto", true, &bsr_ui.settings.sw_gyro);

    bsr_update_settings();
}

/* ====================================================
 *  PAGE 3 : ETAT ROBOT
 * ==================================================== */
static void create_page_status(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, BSR_COL_BG, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, SCREEN_W, 42); lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, BSR_COL_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *t = make_label(hdr, "Etat robot", BSR_COL_TEXT, &lv_font_montserrat_22);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sc = lv_obj_create(parent);
    lv_obj_set_size(sc, SCREEN_W, SCREEN_H - 42); lv_obj_set_pos(sc, 0, 42);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 10, 0);
    lv_obj_set_style_pad_row(sc, 8, 0);
    lv_obj_set_layout(sc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_OFF);

    /* Global status */
    bsr_ui.status.cont_global = lv_obj_create(sc);
    lv_obj_set_width(bsr_ui.status.cont_global, lv_pct(100));
    lv_obj_set_height(bsr_ui.status.cont_global, 45);
    lv_obj_set_style_bg_color(bsr_ui.status.cont_global, lv_color_hex(0x0D3D1F), 0);
    lv_obj_set_style_bg_opa(bsr_ui.status.cont_global, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bsr_ui.status.cont_global, 1, 0);
    lv_obj_set_style_border_color(bsr_ui.status.cont_global, BSR_COL_GREEN, 0);
    lv_obj_set_style_radius(bsr_ui.status.cont_global, 12, 0);
    lv_obj_set_style_pad_left(bsr_ui.status.cont_global, 14, 0);
    lv_obj_set_scrollbar_mode(bsr_ui.status.cont_global, LV_SCROLLBAR_MODE_OFF);

    bsr_ui.status.lbl_global_status = make_label(bsr_ui.status.cont_global,
        "OPERATIONNEL", BSR_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(bsr_ui.status.lbl_global_status, LV_ALIGN_LEFT_MID, 0, 0);

    /* Batteries */
    lv_obj_t *bl = lv_obj_create(sc);
    lv_obj_set_width(bl, lv_pct(100)); lv_obj_set_height(bl, 42);
    set_card_style(bl); lv_obj_set_scrollbar_mode(bl, LV_SCROLLBAR_MODE_OFF);
    make_label(bl, "Batt. G", BSR_COL_TEXT_MID, &lv_font_montserrat_18);
    bsr_ui.status.lbl_batt_l_val = make_label(bl, "36.8V", BSR_COL_TEXT, &lv_font_montserrat_18);
    lv_obj_align(bsr_ui.status.lbl_batt_l_val, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *br = lv_obj_create(sc);
    lv_obj_set_width(br, lv_pct(100)); lv_obj_set_height(br, 42);
    set_card_style(br); lv_obj_set_scrollbar_mode(br, LV_SCROLLBAR_MODE_OFF);
    make_label(br, "Batt. D", BSR_COL_TEXT_MID, &lv_font_montserrat_18);
    bsr_ui.status.lbl_batt_r_val = make_label(br, "36.6V", BSR_COL_TEXT, &lv_font_montserrat_18);
    lv_obj_align(bsr_ui.status.lbl_batt_r_val, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Temperatures */
    lv_obj_t *tc = lv_obj_create(sc);
    lv_obj_set_width(tc, lv_pct(100));
    lv_obj_set_height(tc, LV_SIZE_CONTENT);
    set_card_style(tc);
    lv_obj_set_layout(tc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tc, 6, 0);
    lv_obj_set_scrollbar_mode(tc, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *row = lv_obj_create(tc);
        lv_obj_set_width(row, lv_pct(100)); lv_obj_set_height(row, 26);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

        make_label(row, temp_names[i], BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
        bsr_ui.status.lbl_temp_val[i] = make_label(row, "0C", BSR_COL_TEXT, &lv_font_montserrat_18);
        lv_obj_set_pos(bsr_ui.status.lbl_temp_val[i], 80, 0);

        bsr_ui.status.bar_temp[i] = lv_bar_create(row);
        lv_obj_set_size(bsr_ui.status.bar_temp[i], 100, 5);
        lv_obj_align(bsr_ui.status.bar_temp[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_bar_set_range(bsr_ui.status.bar_temp[i], 0, 100);
        lv_obj_set_style_bg_color(bsr_ui.status.bar_temp[i], BSR_COL_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bsr_ui.status.bar_temp[i], BSR_COL_GREEN, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bsr_ui.status.bar_temp[i], 3, LV_PART_MAIN);
        lv_obj_set_style_radius(bsr_ui.status.bar_temp[i], 3, LV_PART_INDICATOR);
    }

    /* Dernier evenement */
    lv_obj_t *ev = lv_obj_create(sc);
    lv_obj_set_width(ev, lv_pct(100)); lv_obj_set_height(ev, 58);
    set_card_style(ev); lv_obj_set_scrollbar_mode(ev, LV_SCROLLBAR_MODE_OFF);
    bsr_ui.status.lbl_event_text = make_label(ev, "Aucun", BSR_COL_TEXT_MID, &lv_font_montserrat_18);
    bsr_ui.status.lbl_event_time = make_label(ev, "", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_set_pos(bsr_ui.status.lbl_event_time, 0, 20);
}

/* ====================================================
 *  PAGE 4 : CONNEXION
 * ==================================================== */
static void btn_search_cb(lv_event_t *e) {
    (void)e;
    if (s_bind_action_cb) s_bind_action_cb(true);
}
static void btn_disconnect_cb(lv_event_t *e) {
    (void)e;
    if (s_bind_action_cb) s_bind_action_cb(false);
}

static void return_to_drive_cb(lv_timer_t *timer) {
    lv_obj_set_tile(bsr_ui.tileview, bsr_ui.tile_drive, LV_ANIM_ON);
    lv_timer_del(timer);
}

static void create_page_connection(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, BSR_COL_BG, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, SCREEN_W, 42); lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, BSR_COL_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *tt = make_label(hdr, "Connexion", BSR_COL_TEXT, &lv_font_montserrat_22);
    lv_obj_align(tt, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sc = lv_obj_create(parent);
    lv_obj_set_size(sc, SCREEN_W, SCREEN_H - 42); lv_obj_set_pos(sc, 0, 42);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 10, 0);
    lv_obj_set_style_pad_row(sc, 8, 0);
    lv_obj_set_layout(sc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);

    /* Conn status */
    bsr_ui.connection.cont_conn = lv_obj_create(sc);
    lv_obj_set_width(bsr_ui.connection.cont_conn, lv_pct(100));
    lv_obj_set_height(bsr_ui.connection.cont_conn, 60);
    lv_obj_set_style_bg_color(bsr_ui.connection.cont_conn, lv_color_hex(0x0D3D1F), 0);
    lv_obj_set_style_bg_opa(bsr_ui.connection.cont_conn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bsr_ui.connection.cont_conn, 1, 0);
    lv_obj_set_style_border_color(bsr_ui.connection.cont_conn, BSR_COL_GREEN, 0);
    lv_obj_set_style_radius(bsr_ui.connection.cont_conn, 12, 0);
    lv_obj_set_style_pad_left(bsr_ui.connection.cont_conn, 14, 0);
    lv_obj_set_scrollbar_mode(bsr_ui.connection.cont_conn, LV_SCROLLBAR_MODE_OFF);

    bsr_ui.connection.lbl_conn_name = make_label(bsr_ui.connection.cont_conn,
        "ROBOT-01", BSR_COL_TEXT, &lv_font_montserrat_22);
    lv_obj_set_pos(bsr_ui.connection.lbl_conn_name, 0, 6);
    bsr_ui.connection.lbl_conn_detail = make_label(bsr_ui.connection.cont_conn,
        "ESP-NOW - 8ms", BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_set_pos(bsr_ui.connection.lbl_conn_detail, 0, 30);

    /* Buttons */
    bsr_ui.connection.btn_search = lv_btn_create(sc);
    lv_obj_set_width(bsr_ui.connection.btn_search, lv_pct(100));
    lv_obj_set_height(bsr_ui.connection.btn_search, 40);
    lv_obj_set_style_bg_color(bsr_ui.connection.btn_search, BSR_COL_ACCENT, 0);
    lv_obj_set_style_radius(bsr_ui.connection.btn_search, 10, 0);
    lv_obj_add_event_cb(bsr_ui.connection.btn_search, btn_search_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = make_label(bsr_ui.connection.btn_search, "Rechercher",
                               lv_color_hex(0x1A1C22), &lv_font_montserrat_22);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 0);

    bsr_ui.connection.btn_disconnect = lv_btn_create(sc);
    lv_obj_set_width(bsr_ui.connection.btn_disconnect, lv_pct(100));
    lv_obj_set_height(bsr_ui.connection.btn_disconnect, 40);
    lv_obj_set_style_bg_color(bsr_ui.connection.btn_disconnect, BSR_COL_CARD, 0);
    lv_obj_set_style_radius(bsr_ui.connection.btn_disconnect, 10, 0);
    lv_obj_set_style_border_width(bsr_ui.connection.btn_disconnect, 1, 0);
    lv_obj_set_style_border_color(bsr_ui.connection.btn_disconnect, lv_color_hex(0x2E3140), 0);
    lv_obj_add_event_cb(bsr_ui.connection.btn_disconnect, btn_disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = make_label(bsr_ui.connection.btn_disconnect, "Deconnecter",
                               BSR_COL_TEXT_MID, &lv_font_montserrat_20);
    lv_obj_align(dl, LV_ALIGN_CENTER, 0, 0);

    /* Radio card */
    lv_obj_t *rc = lv_obj_create(sc);
    lv_obj_set_width(rc, lv_pct(100));
    lv_obj_set_height(rc, 115);
    set_card_style(rc);
    lv_obj_set_layout(rc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rc, 6, 0);
    lv_obj_set_scrollbar_mode(rc, LV_SCROLLBAR_MODE_OFF);

    /* Radio rows */
    const char *rlabels[] = {"RSSI", "Latence", "Paquets perdus"};
    lv_obj_t **rvals[] = {&bsr_ui.connection.lbl_rssi, &bsr_ui.connection.lbl_latency, &bsr_ui.connection.lbl_pkt_loss};
    const char *rdefaults[] = {"-62 dBm", "8 ms", "0.3%"};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(rc);
        lv_obj_set_width(row, lv_pct(100)); lv_obj_set_height(row, 26);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        make_label(row, rlabels[i], BSR_COL_TEXT_DIM, &lv_font_montserrat_18);
        *rvals[i] = make_label(row, rdefaults[i], BSR_COL_TEXT, &lv_font_montserrat_18);
        lv_obj_align(*rvals[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

/* ====================================================
 *  MISE A JOUR AFFICHAGE
 * ==================================================== */
void bsr_update_drive(void)
{
    char buf[32];
    float abs_spd = fabsf(bsr_data.speed_kmh);
    snprintf(buf, sizeof(buf), "%.1f", abs_spd);
    lv_label_set_text(bsr_ui.drive.lbl_speed_val, buf);

    if (abs_spd < 0.1f) lv_label_set_text(bsr_ui.drive.lbl_speed_dir, "-- ARRET --");
    else if (bsr_data.speed_kmh < 0) lv_label_set_text(bsr_ui.drive.lbl_speed_dir, "ARRIERE");
    else lv_label_set_text(bsr_ui.drive.lbl_speed_dir, "AVANT");

    lv_arc_set_value(bsr_ui.drive.arc_effort, bsr_data.effort_pct);
    snprintf(buf, sizeof(buf), "%d%%", bsr_data.effort_pct);
    lv_label_set_text(bsr_ui.drive.lbl_effort_pct, buf);

    lv_color_t ac = BSR_COL_ACCENT;
    if (bsr_data.effort_pct > 80) ac = BSR_COL_RED;
    else if (bsr_data.effort_pct > 60) ac = BSR_COL_AMBER;
    lv_obj_set_style_arc_color(bsr_ui.drive.arc_effort, ac, LV_PART_INDICATOR);

    if (bsr_data.armed) {
        lv_label_set_text(bsr_ui.drive.lbl_arm_badge, "ARME");
        lv_obj_set_style_text_color(bsr_ui.drive.lbl_arm_badge, BSR_COL_GREEN, 0);
        lv_obj_set_style_bg_color(bsr_ui.drive.lbl_arm_badge, lv_color_hex(0x0D3D1F), 0);
    } else {
        lv_label_set_text(bsr_ui.drive.lbl_arm_badge, "DESARME");
        lv_obj_set_style_text_color(bsr_ui.drive.lbl_arm_badge, BSR_COL_RED, 0);
        lv_obj_set_style_bg_color(bsr_ui.drive.lbl_arm_badge, lv_color_hex(0x3D1A1A), 0);
    }

    lv_label_set_text(bsr_ui.drive.lbl_mode_badge, mode_names[bsr_data.mode]);
    lv_color_t mc;
    switch (bsr_data.mode) {
        case BSR_MODE_MANUEL:    mc = BSR_COL_ACCENT2; break;
        case BSR_MODE_PRECISION: mc = BSR_COL_ACCENT; break;
        case BSR_MODE_BALANCE:   mc = BSR_COL_AMBER; break;
        case BSR_MODE_LOCK:      mc = BSR_COL_RED; break;
        default: mc = BSR_COL_TEXT; break;
    }
    lv_obj_set_style_text_color(bsr_ui.drive.lbl_mode_badge, mc, 0);

    snprintf(buf, sizeof(buf), "%d%%", bsr_data.batt_pct);
    lv_label_set_text(bsr_ui.drive.lbl_batt_pct, buf);
    lv_label_set_text(bsr_ui.drive.lbl_batt_big, buf);
    lv_bar_set_value(bsr_ui.drive.bar_batt, bsr_data.batt_pct, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "~ %.1f km", bsr_data.batt_pct * 0.054f);
    lv_label_set_text(bsr_ui.drive.lbl_batt_range, buf);

    lv_color_t bc = BSR_COL_GREEN;
    if (bsr_data.batt_pct < 15) bc = BSR_COL_RED;
    else if (bsr_data.batt_pct < 30) bc = BSR_COL_AMBER;
    lv_obj_set_style_text_color(bsr_ui.drive.lbl_batt_pct, bc, 0);
    lv_obj_set_style_text_color(bsr_ui.drive.lbl_batt_big, bc, 0);
    lv_obj_set_style_bg_color(bsr_ui.drive.bar_batt, bc, LV_PART_INDICATOR);

    bool alarm = bsr_data.alarm_batt || bsr_data.alarm_temp ||
                 bsr_data.alarm_link_lost || bsr_data.alarm_odrive_fault;
    if (alarm) {
        lv_obj_clear_flag(bsr_ui.drive.alarm_panel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(bsr_ui.drive.lbl_alarm_text, bsr_data.alarm_text);
    } else {
        lv_obj_add_flag(bsr_ui.drive.alarm_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void bsr_update_settings(void)
{
    bsr_drive_mode_t m = bsr_data.mode;
    char buf[32];

    for (int i = 0; i < BSR_MODE_COUNT; i++) {
        if (i == (int)m) {
            lv_obj_set_style_border_color(bsr_ui.settings.btn_mode[i], BSR_COL_ACCENT, 0);
            lv_obj_set_style_bg_color(bsr_ui.settings.btn_mode[i], lv_color_hex(0x1A3D35), 0);
        } else {
            lv_obj_set_style_border_color(bsr_ui.settings.btn_mode[i], lv_color_hex(0x2E3140), 0);
            lv_obj_set_style_bg_color(bsr_ui.settings.btn_mode[i], BSR_COL_CARD, 0);
        }
    }

    snprintf(buf, sizeof(buf), "PERF - %s", mode_names[m]);
    lv_label_set_text(bsr_ui.settings.lbl_perf_mode, buf);

    lv_slider_set_value(bsr_ui.settings.slider_vmax, bsr_data.mode_settings[m].vmax_x10, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%.1f km/h", bsr_data.mode_settings[m].vmax_x10 / 10.0f);
    lv_label_set_text(bsr_ui.settings.lbl_vmax_val, buf);

    lv_slider_set_value(bsr_ui.settings.slider_accel, bsr_data.mode_settings[m].accel, LV_ANIM_OFF);
    lv_label_set_text(bsr_ui.settings.lbl_accel_val, accel_labels[bsr_data.mode_settings[m].accel - 1]);

    lv_slider_set_value(bsr_ui.settings.slider_pmax, bsr_data.mode_settings[m].pmax, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", bsr_data.mode_settings[m].pmax);
    lv_label_set_text(bsr_ui.settings.lbl_pmax_val, buf);

    lv_slider_set_value(bsr_ui.settings.slider_dir_sens, bsr_data.mode_settings[m].dir_sens, LV_ANIM_OFF);
    lv_label_set_text(bsr_ui.settings.lbl_dir_sens_val, dir_sens_labels[bsr_data.mode_settings[m].dir_sens - 1]);

    lv_slider_set_value(bsr_ui.settings.slider_dir_dead, bsr_data.mode_settings[m].dir_dead, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", bsr_data.mode_settings[m].dir_dead);
    lv_label_set_text(bsr_ui.settings.lbl_dir_dead_val, buf);

    bool is_lock = (m == BSR_MODE_LOCK);
    if (is_lock) {
        lv_obj_add_flag(bsr_ui.settings.cont_vmax, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bsr_ui.settings.cont_accel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bsr_ui.settings.cont_direction, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(bsr_ui.settings.cont_vmax, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bsr_ui.settings.cont_accel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bsr_ui.settings.cont_direction, LV_OBJ_FLAG_HIDDEN);
    }
}

void bsr_update_status(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%.1fV %d%% %.0fA",
             bsr_data.batt_voltage_l, bsr_data.batt_pct, bsr_data.batt_current_l);
    lv_label_set_text(bsr_ui.status.lbl_batt_l_val, buf);

    snprintf(buf, sizeof(buf), "%.1fV %d%% %.0fA",
             bsr_data.batt_voltage_r, bsr_data.batt_pct, bsr_data.batt_current_r);
    lv_label_set_text(bsr_ui.status.lbl_batt_r_val, buf);

    int8_t temps[5] = { bsr_data.temp_motor_l, bsr_data.temp_motor_r,
        bsr_data.temp_controller, bsr_data.temp_batt_l, bsr_data.temp_batt_r };
    int8_t max_t = 0;
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "%dC", temps[i]);
        lv_label_set_text(bsr_ui.status.lbl_temp_val[i], buf);
        lv_bar_set_value(bsr_ui.status.bar_temp[i], temps[i] > 100 ? 100 : temps[i], LV_ANIM_ON);
        lv_color_t c = BSR_COL_GREEN;
        if (temps[i] >= 80) c = BSR_COL_RED;
        else if (temps[i] >= 65) c = BSR_COL_AMBER;
        lv_obj_set_style_bg_color(bsr_ui.status.bar_temp[i], c, LV_PART_INDICATOR);
        if (temps[i] > max_t) max_t = temps[i];
    }

    if (max_t >= 80 || bsr_data.alarm_odrive_fault || bsr_data.alarm_link_lost) {
        lv_label_set_text(bsr_ui.status.lbl_global_status, "ARRET");
        lv_obj_set_style_text_color(bsr_ui.status.lbl_global_status, BSR_COL_RED, 0);
        lv_obj_set_style_bg_color(bsr_ui.status.cont_global, lv_color_hex(0x3D1A1A), 0);
        lv_obj_set_style_border_color(bsr_ui.status.cont_global, BSR_COL_RED, 0);
    } else if (max_t >= 65 || bsr_data.batt_pct < 30) {
        lv_label_set_text(bsr_ui.status.lbl_global_status, "ATTENTION");
        lv_obj_set_style_text_color(bsr_ui.status.lbl_global_status, BSR_COL_AMBER, 0);
        lv_obj_set_style_bg_color(bsr_ui.status.cont_global, lv_color_hex(0x3D2E0D), 0);
        lv_obj_set_style_border_color(bsr_ui.status.cont_global, BSR_COL_AMBER, 0);
    } else {
        lv_label_set_text(bsr_ui.status.lbl_global_status, "OPERATIONNEL");
        lv_obj_set_style_text_color(bsr_ui.status.lbl_global_status, BSR_COL_GREEN, 0);
        lv_obj_set_style_bg_color(bsr_ui.status.cont_global, lv_color_hex(0x0D3D1F), 0);
        lv_obj_set_style_border_color(bsr_ui.status.cont_global, BSR_COL_GREEN, 0);
    }
}

void bsr_update_connection(void)
{
    char buf[32];
    if (bsr_data.connected) {
        lv_obj_set_style_bg_color(bsr_ui.connection.cont_conn, lv_color_hex(0x0D3D1F), 0);
        lv_obj_set_style_border_color(bsr_ui.connection.cont_conn, BSR_COL_GREEN, 0);
        lv_label_set_text(bsr_ui.connection.lbl_conn_name, "ROBOT-01");
        snprintf(buf, sizeof(buf), "ESP-NOW - %dms", bsr_data.latency_ms);
        lv_label_set_text(bsr_ui.connection.lbl_conn_detail, buf);
    } else {
        lv_obj_set_style_bg_color(bsr_ui.connection.cont_conn, BSR_COL_CARD, 0);
        lv_obj_set_style_border_color(bsr_ui.connection.cont_conn, lv_color_hex(0x2E3140), 0);
        lv_label_set_text(bsr_ui.connection.lbl_conn_name, "Non connecte");
        lv_label_set_text(bsr_ui.connection.lbl_conn_detail, "--");
    }
    snprintf(buf, sizeof(buf), "%d dBm", bsr_data.rssi);
    lv_label_set_text(bsr_ui.connection.lbl_rssi, buf);
    snprintf(buf, sizeof(buf), "%d ms", bsr_data.latency_ms);
    lv_label_set_text(bsr_ui.connection.lbl_latency, buf);
    snprintf(buf, sizeof(buf), "%.1f%%", bsr_data.pkt_loss_pct);
    lv_label_set_text(bsr_ui.connection.lbl_pkt_loss, buf);
}

void bsr_screen_update(void)
{
    bsr_update_drive();
    bsr_update_settings();
    bsr_update_status();
    bsr_update_connection();
}

void bsr_set_mode(bsr_drive_mode_t mode)
{
    if (mode >= BSR_MODE_COUNT) return;
    bsr_data.mode = mode;
    bsr_update_drive();
    bsr_update_settings();
    notify_settings_changed();
}

void bsr_register_settings_changed_cb(bsr_settings_cb_t cb) { s_settings_cb = cb; }
void bsr_register_bind_action_cb(bsr_bind_action_cb_t cb) { s_bind_action_cb = cb; }

void bsr_show_connection_page(int delay_ms)
{
    lv_obj_set_tile(bsr_ui.tileview, bsr_ui.tile_connection, LV_ANIM_ON);
    if (delay_ms > 0) {
        lv_timer_t *t = lv_timer_create(return_to_drive_cb, (uint32_t)delay_ms, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

static void notify_settings_changed(void)
{
    if (s_settings_cb) s_settings_cb(bsr_data.mode, &bsr_data);
}
