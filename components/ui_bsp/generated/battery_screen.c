/*
 * battery_screen.c
 * Ecran BMS LiFePO4 11S — LVGL sur ESP32-S3 AMOLED 456x280 paysage
 * Adapte de bms_lifepo4_monitor.html
 *
 * Architecture : tileview horizontal (4 tuiles = 4 pages)
 *   Tile 0 : Resume (SOC arc, tension, courant, puissance, temp)
 *   Tile 1 : USB-C (2 ports cliquables + chargeur) + overlay Port Detail
 *   Tile 2 : Tension pack (graph SOC + cap nominale/dynamique)
 *   Tile 3 : Temperatures + MOSFET
 *
 * Polices : toutes les tailles HTML < 20px ont ete augmentees de 8.
 */

#include "battery_screen.h"
#include <stdio.h>
#include <string.h>

/* --- Instances globales --- */
battery_ui_t battery_ui;
bms_data_t   bms_data;

/* --- Constantes paysage --- */
#define SCREEN_W  456
#define SCREEN_H  280

static const char *temp_names[3] = {"Cellule", "BMS", "USB-C"};
static const char *state_names[] = {"CHARGE", "DECHARGE", "REPOS", "ERREUR"};

/* PDO tables */
typedef struct { const char *label; uint8_t v; bool is_epr; } pdo_entry_t;
static const pdo_entry_t pdo_sink[] = {
    {"5V",5,false},{"9V",9,false},{"15V",15,false},{"20V",20,false},
    {"28V",28,true},{"36V",36,true},{"48V",48,true}
};
#define PDO_SINK_COUNT 7

/* --- Helpers --- */
static void bat_set_card_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, BAT_COL_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, BAT_COL_BORDER, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
}

static lv_obj_t *bat_make_label(lv_obj_t *parent, const char *text,
                                 lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

static lv_color_t soc_color(uint8_t soc)
{
    if (soc > 50) return BAT_COL_GREEN;
    if (soc > 20) return BAT_COL_AMBER;
    return BAT_COL_RED;
}

static lv_color_t temp_color(float t)
{
    if (t > 45.0f) return BAT_COL_RED;
    if (t > 35.0f) return BAT_COL_AMBER;
    return BAT_COL_ACCENT;
}

static lv_color_t state_color(bms_state_t s)
{
    switch (s) {
        case BMS_STATE_CHARGE:    return BAT_COL_GREEN;
        case BMS_STATE_DISCHARGE: return BAT_COL_AMBER;
        case BMS_STATE_IDLE:      return BAT_COL_BLUE;
        case BMS_STATE_ERROR:     return BAT_COL_RED;
        default:                  return BAT_COL_TEXT_DIM;
    }
}

static void setup_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
}

static void add_page_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, BAT_COL_CARD, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_80, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, BAT_COL_BORDER, 0);
    lv_obj_set_style_radius(pill, 20, 0);
    lv_obj_set_style_pad_hor(pill, 12, 0);
    lv_obj_set_style_pad_ver(pill, 3, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = bat_make_label(pill, text, BAT_COL_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_center(lbl);
}

/* --- Page dots --- */
static lv_obj_t *s_dots[4];

static void create_dots(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, SCREEN_W, 12);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, 5, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        s_dots[i] = lv_obj_create(cont);
        lv_obj_set_size(s_dots[i], (i == 0) ? 14 : 5, 5);
        lv_obj_set_style_radius(s_dots[i], (i == 0) ? 3 : LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dots[i], (i == 0) ? BAT_COL_ACCENT : lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_pad_all(s_dots[i], 0, 0);
    }
}

static void update_dots(int active)
{
    for (int i = 0; i < 4; i++) {
        bool a = (i == active);
        lv_obj_set_size(s_dots[i], a ? 14 : 5, 5);
        lv_obj_set_style_radius(s_dots[i], a ? 3 : LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dots[i], a ? BAT_COL_ACCENT : lv_color_hex(0x333333), 0);
    }
}

static void hide_port_detail(void);  /* forward decl for tile_changed_cb */

/* Auto-return to page 1 after 30s on pages 2/3/4 */
static lv_timer_t *s_auto_return_timer = NULL;

static void auto_return_timer_cb(lv_timer_t *t)
{
    (void)t;
    /* Navigate back to page 1 */
    lv_obj_set_tile(battery_ui.tileview, battery_ui.tile_resume, LV_ANIM_ON);
    update_dots(0);
    /* Close port detail if open */
    if (!lv_obj_has_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_HIDDEN)) {
        hide_port_detail();
    }
    lv_timer_pause(s_auto_return_timer);
}

static void touch_activity_cb(lv_event_t *e)
{
    (void)e;
    /* Reset auto-return timer on any touch if it's running */
    if (s_auto_return_timer && !s_auto_return_timer->paused) {
        lv_timer_reset(s_auto_return_timer);
    }
}

static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *tile = lv_tileview_get_tile_act(tv);
    lv_coord_t x = lv_obj_get_x(tile) / SCREEN_W;
    update_dots((int)x);

    /* When leaving page 2 (USB-C), close port detail overlay */
    if (tile != battery_ui.tile_usbc && !lv_obj_has_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_HIDDEN)) {
        hide_port_detail();
    }

    /* Auto-return timer: start/reset on pages 2-4, pause on page 1 */
    if (x > 0) {
        lv_timer_reset(s_auto_return_timer);
        lv_timer_resume(s_auto_return_timer);
    } else {
        lv_timer_pause(s_auto_return_timer);
    }
}

/* ====================================================
 *  PAGE 1 : RESUME  (paysage 456x280)
 *  Jauge SOC plein ecran + puissance W
 *  Scroll vers le bas pour voir tension + courant
 * ==================================================== */
static void create_page_resume(lv_obj_t *parent)
{
    setup_tile(parent);
    /* Reactiver scroll vertical pour cette page */
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    /* Status bar — fixed at top */
    lv_obj_t *sb = lv_obj_create(parent);
    lv_obj_set_size(sb, SCREEN_W, 22);
    lv_obj_set_pos(sb, 0, 0);
    lv_obj_set_style_bg_color(sb, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_90, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    lv_obj_set_style_pad_hor(sb, 12, 0);
    lv_obj_set_style_pad_ver(sb, 2, 0);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(sb, "BMS", BAT_COL_ACCENT, &lv_font_montserrat_16);
    battery_ui.resume.lbl_state = bat_make_label(sb, "CHARGE", BAT_COL_GREEN, &lv_font_montserrat_16);
    lv_obj_align(battery_ui.resume.lbl_state, LV_ALIGN_RIGHT_MID, 0, 0);

    /* SOC arc — x1.5 = 390x390, bas aligne au bas ecran */
    battery_ui.resume.arc_soc = lv_arc_create(parent);
    lv_obj_set_size(battery_ui.resume.arc_soc, 390, 390);
    lv_obj_set_pos(battery_ui.resume.arc_soc, (SCREEN_W - 390) / 2, 10);
    lv_arc_set_rotation(battery_ui.resume.arc_soc, 180);
    lv_arc_set_bg_angles(battery_ui.resume.arc_soc, 0, 180);
    lv_arc_set_range(battery_ui.resume.arc_soc, 0, 100);
    lv_arc_set_value(battery_ui.resume.arc_soc, 87);
    lv_obj_remove_style(battery_ui.resume.arc_soc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(battery_ui.resume.arc_soc, 27, LV_PART_MAIN);
    lv_obj_set_style_arc_color(battery_ui.resume.arc_soc, BAT_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(battery_ui.resume.arc_soc, 27, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(battery_ui.resume.arc_soc, BAT_COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(battery_ui.resume.arc_soc, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(battery_ui.resume.arc_soc, LV_OBJ_FLAG_CLICKABLE);

    /* SOC percentage — font 48 (max disponible LVGL) */
    battery_ui.resume.lbl_soc = bat_make_label(parent, "87", BAT_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_pos(battery_ui.resume.lbl_soc, 0, 105);
    lv_obj_set_width(battery_ui.resume.lbl_soc, SCREEN_W);
    lv_obj_set_style_text_align(battery_ui.resume.lbl_soc, LV_TEXT_ALIGN_CENTER, 0);

    battery_ui.resume.lbl_soc_unit = bat_make_label(parent, "%", BAT_COL_TEXT_DIM, &lv_font_montserrat_28);
    lv_obj_align_to(battery_ui.resume.lbl_soc_unit, battery_ui.resume.lbl_soc,
                    LV_ALIGN_OUT_RIGHT_TOP, -174, 4);

    /* Power value — font 48 (+10), bas a y=205 */
    battery_ui.resume.lbl_pow_val = bat_make_label(parent, "186 W", BAT_COL_BLUE, &lv_font_montserrat_48);
    lv_obj_set_pos(battery_ui.resume.lbl_pow_val, 0, 157);
    lv_obj_set_width(battery_ui.resume.lbl_pow_val, SCREEN_W);
    lv_obj_set_style_text_align(battery_ui.resume.lbl_pow_val, LV_TEXT_ALIGN_CENTER, 0);

    /* ── Zone scrollable: cards sous l'ecran visible ── */

    /* Tension card — full width */
    lv_obj_t *c_volt = lv_obj_create(parent);
    lv_obj_set_size(c_volt, SCREEN_W - 16, 56);
    lv_obj_set_pos(c_volt, 8, SCREEN_H + 10);
    bat_set_card_style(c_volt);
    lv_obj_set_style_pad_hor(c_volt, 12, 0);
    lv_obj_set_style_pad_ver(c_volt, 8, 0);
    lv_obj_clear_flag(c_volt, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_volt, "TENSION", BAT_COL_TEXT_DIM, &lv_font_montserrat_36);
    battery_ui.resume.lbl_volt_val = bat_make_label(c_volt, "35.84 V", BAT_COL_TEXT, &lv_font_montserrat_36);
    lv_obj_align(battery_ui.resume.lbl_volt_val, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Courant card — full width */
    lv_obj_t *c_curr = lv_obj_create(parent);
    lv_obj_set_size(c_curr, SCREEN_W - 16, 56);
    lv_obj_set_pos(c_curr, 8, SCREEN_H + 74);
    bat_set_card_style(c_curr);
    lv_obj_set_style_pad_hor(c_curr, 12, 0);
    lv_obj_set_style_pad_ver(c_curr, 8, 0);
    lv_obj_clear_flag(c_curr, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_curr, "COURANT", BAT_COL_TEXT_DIM, &lv_font_montserrat_36);
    battery_ui.resume.lbl_curr_val = bat_make_label(c_curr, "+5.2 A", BAT_COL_GREEN, &lv_font_montserrat_36);
    lv_obj_align(battery_ui.resume.lbl_curr_val, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ====================================================
 *  PAGE 2 : USB-C  (paysage 456x280)
 *  2 port blocks side by side + charger footer
 *  + overlay port detail
 * ==================================================== */

/* Forward declarations */
static void port_click_cb(lv_event_t *e);
static void detail_back_cb(lv_event_t *e);
static void pdo_btn_cb(lv_event_t *e);
static void slider_cb(lv_event_t *e);

static void show_port_detail(int port_idx);
static void hide_port_detail(void);
static void update_port_detail_budget(void);

static void create_page_usbc(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Main view */
    battery_ui.usbc.cont_main = lv_obj_create(parent);
    lv_obj_set_size(battery_ui.usbc.cont_main, SCREEN_W, SCREEN_H);
    lv_obj_align(battery_ui.usbc.cont_main, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(battery_ui.usbc.cont_main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_ui.usbc.cont_main, 0, 0);
    lv_obj_set_style_pad_all(battery_ui.usbc.cont_main, 0, 0);
    lv_obj_clear_flag(battery_ui.usbc.cont_main, LV_OBJ_FLAG_SCROLLABLE);

    /* 2 port blocks side by side — top 2/3 (186px) */
    for (int p = 0; p < 2; p++) {
        lv_obj_t *block = lv_obj_create(battery_ui.usbc.cont_main);
        lv_obj_set_size(block, 212, 186);
        lv_obj_align(block, LV_ALIGN_TOP_LEFT, 8 + p * 222, 0);
        bat_set_card_style(block);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(block, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(block, port_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)p);
        battery_ui.usbc.port_block[p] = block;

        char title[24];
        snprintf(title, sizeof(title), "Port %d", p + 1);
        bat_make_label(block, title, BAT_COL_TEXT, &lv_font_montserrat_20);

        battery_ui.usbc.lbl_p_role[p] = bat_make_label(block, "SINK", BAT_COL_GREEN, &lv_font_montserrat_16);
        lv_obj_align(battery_ui.usbc.lbl_p_role[p], LV_ALIGN_TOP_RIGHT, 0, 0);

        battery_ui.usbc.lbl_p_v[p] = bat_make_label(block, "48.0 V", BAT_COL_TEXT, &lv_font_montserrat_22);
        lv_obj_align(battery_ui.usbc.lbl_p_v[p], LV_ALIGN_BOTTOM_LEFT, 0, -26);

        battery_ui.usbc.lbl_p_a[p] = bat_make_label(block, "5.0 A", BAT_COL_TEXT, &lv_font_montserrat_22);
        lv_obj_align(battery_ui.usbc.lbl_p_a[p], LV_ALIGN_BOTTOM_LEFT, 0, 0);

        battery_ui.usbc.lbl_p_w[p] = bat_make_label(block, "240 W", BAT_COL_AMBER, &lv_font_montserrat_22);
        lv_obj_align(battery_ui.usbc.lbl_p_w[p], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    /* Charger footer — bottom 1/3 (93px) */
    lv_obj_t *footer = lv_obj_create(battery_ui.usbc.cont_main);
    lv_obj_set_size(footer, 436, 83);
    lv_obj_align(footer, LV_ALIGN_TOP_MID, 0, 190);
    bat_set_card_style(footer);
    lv_obj_set_style_pad_all(footer, 6, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    battery_ui.usbc.lbl_chg_phase = bat_make_label(footer, "Charge rapide", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_align(battery_ui.usbc.lbl_chg_phase, LV_ALIGN_LEFT_MID, 0, 0);
    battery_ui.usbc.lbl_chg_temp = bat_make_label(footer, "34C", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.usbc.lbl_chg_temp, LV_ALIGN_RIGHT_MID, 0, 0);
    battery_ui.usbc.lbl_chg_ibat = bat_make_label(footer, "+4.2A", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align_to(battery_ui.usbc.lbl_chg_ibat, battery_ui.usbc.lbl_chg_temp, LV_ALIGN_OUT_LEFT_MID, -12, 0);

    /* ── Port Detail overlay (hidden by default) ── */
    battery_ui.usbc.cont_detail = lv_obj_create(parent);
    lv_obj_set_size(battery_ui.usbc.cont_detail, SCREEN_W, SCREEN_H);
    lv_obj_align(battery_ui.usbc.cont_detail, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(battery_ui.usbc.cont_detail, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(battery_ui.usbc.cont_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_ui.usbc.cont_detail, 0, 0);
    lv_obj_set_style_pad_all(battery_ui.usbc.cont_detail, 0, 0);
    lv_obj_add_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_SCROLLABLE);

    /* Detail header */
    lv_obj_t *dh = lv_obj_create(battery_ui.usbc.cont_detail);
    lv_obj_set_size(dh, SCREEN_W, 34);
    lv_obj_align(dh, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(dh, BAT_COL_CARD, 0);
    lv_obj_set_style_bg_opa(dh, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dh, 0, 0);
    lv_obj_set_style_pad_hor(dh, 10, 0);
    lv_obj_set_style_pad_ver(dh, 4, 0);
    lv_obj_clear_flag(dh, LV_OBJ_FLAG_SCROLLABLE);

    battery_ui.usbc.lbl_detail_title = bat_make_label(dh, "Port 1", BAT_COL_TEXT, &lv_font_montserrat_20);
    battery_ui.usbc.lbl_detail_badge = bat_make_label(dh, "SINK", BAT_COL_GREEN, &lv_font_montserrat_16);
    lv_obj_align(battery_ui.usbc.lbl_detail_badge, LV_ALIGN_LEFT_MID, 76, 0);

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(dh);
    lv_obj_set_size(btn_back, 66, 24);
    lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, BAT_COL_CARD2, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_add_event_cb(btn_back, detail_back_cb, LV_EVENT_CLICKED, NULL);
    bat_make_label(btn_back, "Retour", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);

    /* PDO selector — all 7 buttons in one row */
    bat_make_label(battery_ui.usbc.cont_detail, "TENSION PDO", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_t *pdo_lbl = lv_obj_get_child(battery_ui.usbc.cont_detail, -1);
    lv_obj_align(pdo_lbl, LV_ALIGN_TOP_LEFT, 10, 40);

    lv_obj_t *pdo_row = lv_obj_create(battery_ui.usbc.cont_detail);
    lv_obj_set_size(pdo_row, 430, 32);
    lv_obj_align(pdo_row, LV_ALIGN_TOP_LEFT, 10, 58);
    lv_obj_set_style_bg_opa(pdo_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pdo_row, 0, 0);
    lv_obj_set_style_pad_all(pdo_row, 0, 0);
    lv_obj_set_flex_flow(pdo_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pdo_row, 4, 0);
    lv_obj_clear_flag(pdo_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < PDO_SINK_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(pdo_row);
        lv_obj_set_size(btn, 56, 28);
        lv_obj_set_style_bg_color(btn, BAT_COL_CARD2, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, BAT_COL_BORDER, 0);
        lv_obj_add_event_cb(btn, pdo_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = bat_make_label(btn, pdo_sink[i].label, BAT_COL_TEXT_DIM, &lv_font_montserrat_16);
        lv_obj_center(lbl);
        battery_ui.usbc.pdo_btns[i] = btn;
    }

    /* Current slider */
    bat_make_label(battery_ui.usbc.cont_detail, "COURANT ALLOUE", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_t *clbl = lv_obj_get_child(battery_ui.usbc.cont_detail, -1);
    lv_obj_align(clbl, LV_ALIGN_TOP_LEFT, 10, 96);

    battery_ui.usbc.lbl_slider_val = bat_make_label(battery_ui.usbc.cont_detail, "2.5 A", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.usbc.lbl_slider_val, LV_ALIGN_TOP_LEFT, 160, 93);

    battery_ui.usbc.slider_current = lv_slider_create(battery_ui.usbc.cont_detail);
    lv_obj_set_size(battery_ui.usbc.slider_current, 430, 10);
    lv_obj_align(battery_ui.usbc.slider_current, LV_ALIGN_TOP_LEFT, 10, 118);
    lv_slider_set_range(battery_ui.usbc.slider_current, 0, 50);
    lv_slider_set_value(battery_ui.usbc.slider_current, 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_ui.usbc.slider_current, BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_ui.usbc.slider_current, BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(battery_ui.usbc.slider_current, BAT_COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(battery_ui.usbc.slider_current, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(battery_ui.usbc.slider_current, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Scale labels 0-5A */
    const char *scale_labels[] = {"0A","1A","2A","3A","4A","5A"};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *sl = bat_make_label(battery_ui.usbc.cont_detail, scale_labels[i],
                                       BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
        lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 10 + i * 86, 132);
    }

    /* Power summary: 3 cards in a row */
    const char *pwr_titles[] = {"PUISS. P1", "PUISS. P2", "TOTAL"};
    lv_obj_t **pwr_lbls[] = {
        &battery_ui.usbc.lbl_pwr_p1,
        &battery_ui.usbc.lbl_pwr_p2,
        &battery_ui.usbc.lbl_pwr_total
    };
    lv_color_t pwr_cols[] = {BAT_COL_ACCENT, BAT_COL_BLUE, BAT_COL_AMBER};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = lv_obj_create(battery_ui.usbc.cont_detail);
        lv_obj_set_size(card, 140, 58);
        lv_obj_align(card, LV_ALIGN_TOP_LEFT, 8 + i * 148, 152);
        bat_set_card_style(card);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        bat_make_label(card, pwr_titles[i], BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
        *pwr_lbls[i] = bat_make_label(card, "---", pwr_cols[i], &lv_font_montserrat_22);
        lv_obj_align(*pwr_lbls[i], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    battery_ui.usbc.detail_port_idx = 0;
}

/* Port click -> show detail overlay */
static void port_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    show_port_detail(idx);
}

static void detail_back_cb(lv_event_t *e)
{
    (void)e;
    hide_port_detail();
}

static void show_port_detail(int port_idx)
{
    battery_ui.usbc.detail_port_idx = port_idx;
    char buf[24];

    /* Update header */
    snprintf(buf, sizeof(buf), "Port %d", port_idx + 1);
    lv_label_set_text(battery_ui.usbc.lbl_detail_title, buf);

    if (bms_data.usb_port[port_idx].connected) {
        const char *role = bms_data.usb_port[port_idx].is_sink ? "SINK" : "SOURCE";
        lv_label_set_text(battery_ui.usbc.lbl_detail_badge, role);
        lv_obj_set_style_text_color(battery_ui.usbc.lbl_detail_badge,
            bms_data.usb_port[port_idx].is_sink ? BAT_COL_GREEN : BAT_COL_BLUE, 0);
    } else {
        lv_label_set_text(battery_ui.usbc.lbl_detail_badge, "---");
        lv_obj_set_style_text_color(battery_ui.usbc.lbl_detail_badge, BAT_COL_TEXT_DIM, 0);
    }

    /* Update PDO highlights */
    int active_v = (int)bms_data.usb_port[port_idx].contract_v;
    for (int i = 0; i < PDO_SINK_COUNT; i++) {
        bool sel = (pdo_sink[i].v == active_v);
        lv_color_t bg = sel ? (pdo_sink[i].is_epr ? lv_color_hex(0x1A1A30) : lv_color_hex(0x0F1A0F)) : BAT_COL_CARD2;
        lv_color_t bc = sel ? (pdo_sink[i].is_epr ? BAT_COL_ACCENT2 : BAT_COL_GREEN) : BAT_COL_BORDER;
        lv_obj_set_style_bg_color(battery_ui.usbc.pdo_btns[i], bg, 0);
        lv_obj_set_style_border_color(battery_ui.usbc.pdo_btns[i], bc, 0);
        lv_obj_t *lbl = lv_obj_get_child(battery_ui.usbc.pdo_btns[i], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            sel ? (pdo_sink[i].is_epr ? BAT_COL_ACCENT2 : BAT_COL_GREEN) : BAT_COL_TEXT_DIM, 0);
    }

    /* Update slider */
    int sv = (int)(bms_data.usb_port[port_idx].contract_i * 10.0f);
    lv_slider_set_value(battery_ui.usbc.slider_current, sv, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%.1f A", bms_data.usb_port[port_idx].contract_i);
    lv_label_set_text(battery_ui.usbc.lbl_slider_val, buf);

    update_port_detail_budget();

    /* Show overlay, hide main */
    lv_obj_add_flag(battery_ui.usbc.cont_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_HIDDEN);
}

static void hide_port_detail(void)
{
    lv_obj_add_flag(battery_ui.usbc.cont_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_ui.usbc.cont_main, LV_OBJ_FLAG_HIDDEN);
}

static void pdo_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int port = battery_ui.usbc.detail_port_idx;
    bms_data.usb_port[port].contract_v = (float)pdo_sink[idx].v;
    show_port_detail(port);
}

static void slider_cb(lv_event_t *e)
{
    (void)e;
    int val = lv_slider_get_value(battery_ui.usbc.slider_current);
    float amps = val / 10.0f;
    int port = battery_ui.usbc.detail_port_idx;
    bms_data.usb_port[port].contract_i = amps;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f A", amps);
    lv_label_set_text(battery_ui.usbc.lbl_slider_val, buf);
    update_port_detail_budget();
}

static void update_port_detail_budget(void)
{
    char buf[16];
    float p1 = bms_data.usb_port[0].contract_v * bms_data.usb_port[0].contract_i;
    float p2 = bms_data.usb_port[1].contract_v * bms_data.usb_port[1].contract_i;

    snprintf(buf, sizeof(buf), "%.0fW", p1);
    lv_label_set_text(battery_ui.usbc.lbl_pwr_p1, buf);
    snprintf(buf, sizeof(buf), "%.0fW", p2);
    lv_label_set_text(battery_ui.usbc.lbl_pwr_p2, buf);
    snprintf(buf, sizeof(buf), "%.0fW", p1 + p2);
    lv_label_set_text(battery_ui.usbc.lbl_pwr_total, buf);
}

/* ====================================================
 *  PAGE 3 : TENSION PACK  (paysage 456x280)
 *  [3 summary cards row] + [SOC graph left] + [cap cards right]
 * ==================================================== */
static void create_page_voltage(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Summary: 3 cards in a row */
    lv_obj_t *sum_row = lv_obj_create(parent);
    lv_obj_set_size(sum_row, 436, 86);
    lv_obj_align(sum_row, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_opa(sum_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sum_row, 0, 0);
    lv_obj_set_style_pad_all(sum_row, 0, 0);
    lv_obj_set_flex_flow(sum_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sum_row, 4, 0);
    lv_obj_clear_flag(sum_row, LV_OBJ_FLAG_SCROLLABLE);

    const char *sum_titles[] = {"TOTALE", "MOY.", "DELTA"};
    lv_obj_t **sum_lbls[] = {
        &battery_ui.volt.lbl_total,
        &battery_ui.volt.lbl_avg,
        &battery_ui.volt.lbl_delta
    };
    const char *sum_vals[] = {"35.84V", "3.258V", "8mV"};
    lv_color_t sum_cols[] = {BAT_COL_TEXT, BAT_COL_ACCENT, BAT_COL_AMBER};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = lv_obj_create(sum_row);
        lv_obj_set_height(c, 86);
        lv_obj_set_flex_grow(c, 1);
        bat_set_card_style(c);
        lv_obj_set_style_pad_all(c, 6, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        bat_make_label(c, sum_titles[i], BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
        *sum_lbls[i] = bat_make_label(c, sum_vals[i], sum_cols[i], &lv_font_montserrat_22);
        lv_obj_align(*sum_lbls[i], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    /* SOC Chart — 2 cols x 2 rows */
    lv_obj_t *chart_card = lv_obj_create(parent);
    lv_obj_set_size(chart_card, 288, 176);
    lv_obj_align(chart_card, LV_ALIGN_TOP_LEFT, 10, 92);
    bat_set_card_style(chart_card);
    lv_obj_set_style_pad_all(chart_card, 6, 0);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(chart_card, "SOC  -  2 derniers cycles", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);

    battery_ui.volt.chart = lv_chart_create(chart_card);
    lv_obj_set_size(battery_ui.volt.chart, 268, 142);
    lv_obj_align(battery_ui.volt.chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(battery_ui.volt.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(battery_ui.volt.chart, SOC_GRAPH_POINTS);
    lv_chart_set_range(battery_ui.volt.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(battery_ui.volt.chart, 4, 0);
    lv_obj_set_style_bg_opa(battery_ui.volt.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_ui.volt.chart, 1, 0);
    lv_obj_set_style_border_color(battery_ui.volt.chart, lv_color_hex(0x333333), 0);
    lv_obj_set_style_line_color(battery_ui.volt.chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(battery_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(battery_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_width(battery_ui.volt.chart, 3, LV_PART_INDICATOR);
    lv_obj_set_style_height(battery_ui.volt.chart, 3, LV_PART_INDICATOR);

    battery_ui.volt.ser_soc = lv_chart_add_series(battery_ui.volt.chart,
                                                    BAT_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(battery_ui.volt.chart, 2, LV_PART_ITEMS);

    /* Pre-fill with simulated SOC curve */
    for (int i = 0; i < SOC_GRAPH_POINTS; i++) {
        int v;
        if (i < SOC_GRAPH_POINTS / 2) {
            v = 95 - (i * 150 / SOC_GRAPH_POINTS);
        } else {
            int j = i - SOC_GRAPH_POINTS / 2;
            v = 20 + (j * 134 / SOC_GRAPH_POINTS);
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_chart_set_next_value(battery_ui.volt.chart, battery_ui.volt.ser_soc, v);
    }

    /* Cap nominale + dynamique — right column */
    lv_obj_t *cn = lv_obj_create(parent);
    lv_obj_set_size(cn, 142, 86);
    lv_obj_align(cn, LV_ALIGN_TOP_RIGHT, -10, 92);
    bat_set_card_style(cn);
    lv_obj_set_style_pad_all(cn, 6, 0);
    lv_obj_clear_flag(cn, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cn, "CAP. NOM.", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    battery_ui.volt.lbl_cap_nom_sub = bat_make_label(cn, "96.3%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(battery_ui.volt.lbl_cap_nom_sub, LV_ALIGN_LEFT_MID, 0, 0);
    battery_ui.volt.lbl_cap_nom_val = bat_make_label(cn, "38.50Ah", BAT_COL_ACCENT2, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.volt.lbl_cap_nom_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *cd = lv_obj_create(parent);
    lv_obj_set_size(cd, 142, 86);
    lv_obj_align(cd, LV_ALIGN_TOP_RIGHT, -10, 182);
    bat_set_card_style(cd);
    lv_obj_set_style_pad_all(cd, 6, 0);
    lv_obj_clear_flag(cd, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cd, "CAP. DYN.", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    battery_ui.volt.lbl_cap_dyn_sub = bat_make_label(cd, "90.6%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(battery_ui.volt.lbl_cap_dyn_sub, LV_ALIGN_LEFT_MID, 0, 0);
    battery_ui.volt.lbl_cap_dyn_val = bat_make_label(cd, "34.90Ah", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.volt.lbl_cap_dyn_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

/* ====================================================
 *  PAGE 4 : TEMPERATURES + MOSFET  (paysage 456x280)
 *  Left: temp bars + fan   Right: MOS CHG + MOS DSC
 * ==================================================== */
static void fan_slider_cb(lv_event_t *e)
{
    (void)e;
    int val = lv_slider_get_value(battery_ui.temp.slider_fan);
    bms_data.fan_pct = val;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", val);
    lv_label_set_text(battery_ui.temp.lbl_fan_val, buf);
}

static void create_page_temp(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Left: Temp bars — full height to top */
    lv_obj_t *temp_card = lv_obj_create(parent);
    lv_obj_set_size(temp_card, 260, 168);
    lv_obj_align(temp_card, LV_ALIGN_TOP_LEFT, 8, 0);
    bat_set_card_style(temp_card);
    lv_obj_set_flex_flow(temp_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(temp_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(temp_card, 6, 0);
    lv_obj_clear_flag(temp_card, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(temp_card);
        lv_obj_set_size(row, 240, 36);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *tlbl = bat_make_label(row, temp_names[i], BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
        lv_obj_align(tlbl, LV_ALIGN_LEFT_MID, 0, 0);

        battery_ui.temp.bar_temp[i] = lv_bar_create(row);
        lv_obj_set_size(battery_ui.temp.bar_temp[i], 70, 6);
        lv_obj_align(battery_ui.temp.bar_temp[i], LV_ALIGN_CENTER, 0, 0);
        lv_bar_set_range(battery_ui.temp.bar_temp[i], 0, 70);
        lv_bar_set_value(battery_ui.temp.bar_temp[i], 25, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(battery_ui.temp.bar_temp[i], BAT_COL_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(battery_ui.temp.bar_temp[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(battery_ui.temp.bar_temp[i], 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(battery_ui.temp.bar_temp[i], BAT_COL_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_radius(battery_ui.temp.bar_temp[i], 3, LV_PART_INDICATOR);

        battery_ui.temp.lbl_temp_val[i] = bat_make_label(row, "25 C", BAT_COL_ACCENT, &lv_font_montserrat_22);
        lv_obj_align(battery_ui.temp.lbl_temp_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    /* Left: Fan card — extended to bottom */
    lv_obj_t *fan_card = lv_obj_create(parent);
    lv_obj_set_size(fan_card, 260, 104);
    lv_obj_align(fan_card, LV_ALIGN_TOP_LEFT, 8, 172);
    bat_set_card_style(fan_card);
    lv_obj_clear_flag(fan_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(fan_card, "VENTILATEUR", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    battery_ui.temp.lbl_fan_val = bat_make_label(fan_card, "35 %", BAT_COL_ACCENT, &lv_font_montserrat_20);
    lv_obj_align(battery_ui.temp.lbl_fan_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    battery_ui.temp.slider_fan = lv_slider_create(fan_card);
    lv_obj_set_size(battery_ui.temp.slider_fan, 230, 16);
    lv_obj_align(battery_ui.temp.slider_fan, LV_ALIGN_CENTER, 0, 10);
    lv_slider_set_range(battery_ui.temp.slider_fan, 0, 100);
    lv_slider_set_value(battery_ui.temp.slider_fan, 35, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_ui.temp.slider_fan, BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_ui.temp.slider_fan, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_ui.temp.slider_fan, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_ui.temp.slider_fan, BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(battery_ui.temp.slider_fan, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(battery_ui.temp.slider_fan, BAT_COL_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(battery_ui.temp.slider_fan, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(battery_ui.temp.slider_fan, fan_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Right: CHG badge (top) — stacked vertically */
    lv_coord_t mos_x = 272;
    lv_coord_t mos_w = 456 - mos_x - 8;  /* 176px */

    battery_ui.temp.cont_mos_chg = lv_obj_create(parent);
    lv_obj_set_size(battery_ui.temp.cont_mos_chg, mos_w, 130);
    lv_obj_set_pos(battery_ui.temp.cont_mos_chg, mos_x, 8);
    lv_obj_set_style_bg_color(battery_ui.temp.cont_mos_chg, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(battery_ui.temp.cont_mos_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_ui.temp.cont_mos_chg, 2, 0);
    lv_obj_set_style_border_color(battery_ui.temp.cont_mos_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(battery_ui.temp.cont_mos_chg, LV_OPA_60, 0);
    lv_obj_set_style_radius(battery_ui.temp.cont_mos_chg, 10, 0);
    lv_obj_clear_flag(battery_ui.temp.cont_mos_chg, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(battery_ui.temp.cont_mos_chg, "CHG", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_t *chg_lbl = lv_obj_get_child(battery_ui.temp.cont_mos_chg, 0);
    lv_obj_align(chg_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_chg = lv_obj_create(battery_ui.temp.cont_mos_chg);
    lv_obj_set_size(dot_chg, 18, 18);
    lv_obj_set_style_radius(dot_chg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_chg, 0, 0);
    lv_obj_set_style_shadow_width(dot_chg, 10, 0);
    lv_obj_set_style_shadow_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_chg, LV_OPA_60, 0);
    lv_obj_align(dot_chg, LV_ALIGN_CENTER, 0, 0);

    battery_ui.temp.lbl_mos_chg_state = bat_make_label(battery_ui.temp.cont_mos_chg, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.temp.lbl_mos_chg_state, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Right: DSC badge (bottom) */
    battery_ui.temp.cont_mos_dsc = lv_obj_create(parent);
    lv_obj_set_size(battery_ui.temp.cont_mos_dsc, mos_w, 130);
    lv_obj_set_pos(battery_ui.temp.cont_mos_dsc, mos_x, 144);
    lv_obj_set_style_bg_color(battery_ui.temp.cont_mos_dsc, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(battery_ui.temp.cont_mos_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_ui.temp.cont_mos_dsc, 2, 0);
    lv_obj_set_style_border_color(battery_ui.temp.cont_mos_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(battery_ui.temp.cont_mos_dsc, LV_OPA_60, 0);
    lv_obj_set_style_radius(battery_ui.temp.cont_mos_dsc, 10, 0);
    lv_obj_clear_flag(battery_ui.temp.cont_mos_dsc, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(battery_ui.temp.cont_mos_dsc, "DSC", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_t *dsc_lbl = lv_obj_get_child(battery_ui.temp.cont_mos_dsc, 0);
    lv_obj_align(dsc_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_dsc = lv_obj_create(battery_ui.temp.cont_mos_dsc);
    lv_obj_set_size(dot_dsc, 18, 18);
    lv_obj_set_style_radius(dot_dsc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_dsc, 0, 0);
    lv_obj_set_style_shadow_width(dot_dsc, 10, 0);
    lv_obj_set_style_shadow_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_dsc, LV_OPA_60, 0);
    lv_obj_align(dot_dsc, LV_ALIGN_CENTER, 0, 0);

    battery_ui.temp.lbl_mos_dsc_state = bat_make_label(battery_ui.temp.cont_mos_dsc, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(battery_ui.temp.lbl_mos_dsc_state, LV_ALIGN_BOTTOM_MID, 0, -4);
}

/* ====================================================
 *  INIT
 * ==================================================== */
void battery_screen_init(void)
{
    /* Valeurs par defaut */
    memset(&bms_data, 0, sizeof(bms_data));
    bms_data.state = BMS_STATE_CHARGE;
    bms_data.soc = 87;
    bms_data.voltage = 35.84f;
    bms_data.current = 5.2f;
    bms_data.power = 186.0f;
    bms_data.temp_cell = 24.7f;
    bms_data.temp_bms = 26.3f;
    bms_data.temp_usbc = 31.4f;
    bms_data.cell_avg = 3.258f;
    bms_data.cell_delta = 8;
    bms_data.fan_pct = 35;
    bms_data.mos_chg = true;
    bms_data.mos_dsc = true;
    bms_data.cycles = 12;
    bms_data.cap_nominal = 38500;
    bms_data.cap_dynamic = 34900;
    bms_data.cap_origin = 40000;
    snprintf(bms_data.charge_phase, sizeof(bms_data.charge_phase), "Charge rapide");

    uint16_t default_cells[BMS_CELL_COUNT] = {
        3260, 3258, 3262, 3255, 3261, 3259, 3257, 3263, 3258, 3260, 3256
    };
    memcpy(bms_data.cells_mv, default_cells, sizeof(default_cells));

    bms_data.usb_port[0].connected = true;
    bms_data.usb_port[0].is_sink = true;
    bms_data.usb_port[0].voltage = 48.0f;
    bms_data.usb_port[0].current = 5.0f;
    bms_data.usb_port[0].power = 240.0f;
    bms_data.usb_port[0].contract_v = 48.0f;
    bms_data.usb_port[0].contract_i = 5.0f;
    bms_data.usb_port[1].connected = false;
    bms_data.usb_port[1].contract_v = 5.0f;
    bms_data.usb_port[1].contract_i = 0.0f;
    bms_data.ibat = 4.2f;
    bms_data.charger_temp = 34.0f;

    /* Ecran principal */
    memset(&battery_ui, 0, sizeof(battery_ui));
    battery_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(battery_ui.screen, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(battery_ui.screen, LV_OPA_COVER, 0);

    /* Tileview horizontal */
    battery_ui.tileview = lv_tileview_create(battery_ui.screen);
    lv_obj_set_size(battery_ui.tileview, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(battery_ui.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(battery_ui.tileview, LV_SCROLLBAR_MODE_OFF);

    battery_ui.tile_resume  = lv_tileview_add_tile(battery_ui.tileview, 0, 0, LV_DIR_RIGHT);
    battery_ui.tile_usbc    = lv_tileview_add_tile(battery_ui.tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    battery_ui.tile_voltage = lv_tileview_add_tile(battery_ui.tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    battery_ui.tile_temp    = lv_tileview_add_tile(battery_ui.tileview, 3, 0, LV_DIR_LEFT);

    create_page_resume(battery_ui.tile_resume);
    create_page_usbc(battery_ui.tile_usbc);
    create_page_voltage(battery_ui.tile_voltage);
    create_page_temp(battery_ui.tile_temp);

    create_dots(battery_ui.screen);
    lv_obj_add_event_cb(battery_ui.tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(battery_ui.screen, touch_activity_cb, LV_EVENT_PRESSED, NULL);

    /* Auto-return timer: 30s, starts paused */
    s_auto_return_timer = lv_timer_create(auto_return_timer_cb, 30000, NULL);
    lv_timer_pause(s_auto_return_timer);

    lv_scr_load(battery_ui.screen);
}

/* ====================================================
 *  CLEANUP  — a appeler AVANT de supprimer l'ecran
 * ==================================================== */
void battery_screen_cleanup(void)
{
    /* Supprimer le timer auto-return pour eviter callback sur objets detruits */
    if (s_auto_return_timer) {
        lv_timer_del(s_auto_return_timer);
        s_auto_return_timer = NULL;
    }
}

/* ====================================================
 *  UPDATE
 * ==================================================== */
void battery_screen_update(void)
{
    char buf[32];

    /* Page 1 : Resume */
    lv_label_set_text(battery_ui.resume.lbl_state, state_names[bms_data.state]);
    lv_obj_set_style_text_color(battery_ui.resume.lbl_state, state_color(bms_data.state), 0);

    lv_arc_set_value(battery_ui.resume.arc_soc, bms_data.soc);
    lv_obj_set_style_arc_color(battery_ui.resume.arc_soc, soc_color(bms_data.soc), LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%d", bms_data.soc);
    lv_label_set_text(battery_ui.resume.lbl_soc, buf);

    snprintf(buf, sizeof(buf), "%.2f V", bms_data.voltage);
    lv_label_set_text(battery_ui.resume.lbl_volt_val, buf);

    snprintf(buf, sizeof(buf), "%+.1f A", bms_data.current);
    lv_label_set_text(battery_ui.resume.lbl_curr_val, buf);
    lv_obj_set_style_text_color(battery_ui.resume.lbl_curr_val,
        bms_data.current > 0 ? BAT_COL_GREEN :
        bms_data.current < 0 ? BAT_COL_AMBER : BAT_COL_TEXT_DIM, 0);

    snprintf(buf, sizeof(buf), "%.0f W", bms_data.power);
    lv_label_set_text(battery_ui.resume.lbl_pow_val, buf);
    lv_obj_set_style_text_color(battery_ui.resume.lbl_pow_val,
        (bms_data.state == BMS_STATE_CHARGE) ? BAT_COL_GREEN : BAT_COL_BLUE, 0);

    /* Page 2 : USB-C */
    for (int p = 0; p < 2; p++) {
        if (bms_data.usb_port[p].connected) {
            lv_label_set_text(battery_ui.usbc.lbl_p_role[p],
                bms_data.usb_port[p].is_sink ? "SINK" : "SOURCE");
            lv_obj_set_style_text_color(battery_ui.usbc.lbl_p_role[p],
                bms_data.usb_port[p].is_sink ? BAT_COL_GREEN : BAT_COL_BLUE, 0);
            snprintf(buf, sizeof(buf), "%.1f V", bms_data.usb_port[p].voltage);
            lv_label_set_text(battery_ui.usbc.lbl_p_v[p], buf);
            snprintf(buf, sizeof(buf), "%.1f A", bms_data.usb_port[p].current);
            lv_label_set_text(battery_ui.usbc.lbl_p_a[p], buf);
            snprintf(buf, sizeof(buf), "%.0f W", bms_data.usb_port[p].power);
            lv_label_set_text(battery_ui.usbc.lbl_p_w[p], buf);
        } else {
            lv_label_set_text(battery_ui.usbc.lbl_p_role[p], "---");
            lv_obj_set_style_text_color(battery_ui.usbc.lbl_p_role[p], BAT_COL_TEXT_DIM, 0);
            lv_label_set_text(battery_ui.usbc.lbl_p_v[p], "--- V");
            lv_label_set_text(battery_ui.usbc.lbl_p_a[p], "--- A");
            lv_label_set_text(battery_ui.usbc.lbl_p_w[p], "--- W");
            lv_obj_set_style_text_color(battery_ui.usbc.lbl_p_v[p], BAT_COL_TEXT_DIM, 0);
            lv_obj_set_style_text_color(battery_ui.usbc.lbl_p_a[p], BAT_COL_TEXT_DIM, 0);
            lv_obj_set_style_text_color(battery_ui.usbc.lbl_p_w[p], BAT_COL_TEXT_DIM, 0);
        }
    }

    lv_label_set_text(battery_ui.usbc.lbl_chg_phase, bms_data.charge_phase);
    snprintf(buf, sizeof(buf), "%+.1fA", bms_data.ibat);
    lv_label_set_text(battery_ui.usbc.lbl_chg_ibat, buf);
    snprintf(buf, sizeof(buf), "%.0fC", bms_data.charger_temp);
    lv_label_set_text(battery_ui.usbc.lbl_chg_temp, buf);

    /* Page 3 : Tension */
    snprintf(buf, sizeof(buf), "%.2fV", bms_data.voltage);
    lv_label_set_text(battery_ui.volt.lbl_total, buf);
    snprintf(buf, sizeof(buf), "%.3fV", bms_data.cell_avg);
    lv_label_set_text(battery_ui.volt.lbl_avg, buf);
    snprintf(buf, sizeof(buf), "%dmV", bms_data.cell_delta);
    lv_label_set_text(battery_ui.volt.lbl_delta, buf);

    snprintf(buf, sizeof(buf), "%.2fAh", bms_data.cap_nominal / 1000.0f);
    lv_label_set_text(battery_ui.volt.lbl_cap_nom_val, buf);
    if (bms_data.cap_origin > 0) {
        snprintf(buf, sizeof(buf), "%.1f%%", bms_data.cap_nominal * 100.0f / bms_data.cap_origin);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(battery_ui.volt.lbl_cap_nom_sub, buf);

    snprintf(buf, sizeof(buf), "%.2fAh", bms_data.cap_dynamic / 1000.0f);
    lv_label_set_text(battery_ui.volt.lbl_cap_dyn_val, buf);
    if (bms_data.cap_nominal > 0) {
        snprintf(buf, sizeof(buf), "%.1f%%", bms_data.cap_dynamic * 100.0f / bms_data.cap_nominal);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(battery_ui.volt.lbl_cap_dyn_sub, buf);

    /* Page 4 : Temperatures + MOS */
    float temps[3] = { bms_data.temp_cell, bms_data.temp_bms, bms_data.temp_usbc };
    for (int i = 0; i < 3; i++) {
        lv_bar_set_value(battery_ui.temp.bar_temp[i], (int32_t)temps[i], LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "%.1f C", temps[i]);
        lv_label_set_text(battery_ui.temp.lbl_temp_val[i], buf);
        lv_color_t tc = temp_color(temps[i]);
        lv_obj_set_style_text_color(battery_ui.temp.lbl_temp_val[i], tc, 0);
        lv_obj_set_style_bg_color(battery_ui.temp.bar_temp[i], tc, LV_PART_INDICATOR);
    }

    lv_slider_set_value(battery_ui.temp.slider_fan, bms_data.fan_pct, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d %%", bms_data.fan_pct);
    lv_label_set_text(battery_ui.temp.lbl_fan_val, buf);

    /* MOS CHG */
    lv_color_t chg_col = bms_data.mos_chg ? BAT_COL_GREEN : BAT_COL_RED;
    lv_label_set_text(battery_ui.temp.lbl_mos_chg_state, bms_data.mos_chg ? "ON" : "OFF");
    lv_obj_set_style_text_color(battery_ui.temp.lbl_mos_chg_state, chg_col, 0);
    lv_obj_set_style_border_color(battery_ui.temp.cont_mos_chg, chg_col, 0);
    lv_obj_set_style_bg_color(battery_ui.temp.cont_mos_chg,
        bms_data.mos_chg ? lv_color_hex(0x0F1A0F) : lv_color_hex(0x1A0F0F), 0);

    /* MOS DSC */
    lv_color_t dsc_col = bms_data.mos_dsc ? BAT_COL_GREEN : BAT_COL_RED;
    lv_label_set_text(battery_ui.temp.lbl_mos_dsc_state, bms_data.mos_dsc ? "ON" : "OFF");
    lv_obj_set_style_text_color(battery_ui.temp.lbl_mos_dsc_state, dsc_col, 0);
    lv_obj_set_style_border_color(battery_ui.temp.cont_mos_dsc, dsc_col, 0);
    lv_obj_set_style_bg_color(battery_ui.temp.cont_mos_dsc,
        bms_data.mos_dsc ? lv_color_hex(0x0F1A0F) : lv_color_hex(0x1A0F0F), 0);
}
