/*
 * battery_unplugged_screen.c
 * Ecran BMS LiFePO4 11S deconnecte — LVGL sur ESP32-S3 AMOLED 456x280 paysage
 * Variante de battery_screen.c :
 *   - Pas de page USB-C (page 2 supprimee)
 *   - Page temp : seulement Cellule + BMS (pas USB-C temp)
 *   - 3 pages au lieu de 4, 3 dots
 *
 * Architecture : tileview horizontal (3 tuiles)
 *   Tile 0 : Resume (SOC arc, tension, courant, puissance)
 *   Tile 1 : Tension pack (graph SOC + cap nominale/dynamique)
 *   Tile 2 : Temperatures (Cellule + BMS) + MOSFET
 */

#include "battery_unplugged_screen.h"
#include <stdio.h>
#include <string.h>

/* --- Instances globales --- */
bat_unp_ui_t bat_unp_ui;

/* --- Constantes paysage --- */
#define SCREEN_W  456
#define SCREEN_H  280

#define NUM_PAGES 3

static const char *state_names[] = {"CHARGE", "DECHARGE", "REPOS", "ERREUR"};

/* --- Helpers (memes que battery_screen) --- */
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

/* --- Dots navigation (3 dots) --- */
static lv_obj_t *s_dots[NUM_PAGES];

static void create_dots(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), 12);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, 5, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < NUM_PAGES; i++) {
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
    for (int i = 0; i < NUM_PAGES; i++) {
        bool a = (i == active);
        lv_obj_set_size(s_dots[i], a ? 14 : 5, 5);
        lv_obj_set_style_radius(s_dots[i], a ? 3 : LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dots[i], a ? BAT_COL_ACCENT : lv_color_hex(0x333333), 0);
    }
}

/* --- Auto-return timer (30s) --- */
static lv_timer_t *s_auto_return_timer = NULL;

static void auto_return_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_set_tile(bat_unp_ui.tileview, bat_unp_ui.tile_resume, LV_ANIM_ON);
    update_dots(0);
    lv_timer_pause(s_auto_return_timer);
}

static void touch_activity_cb(lv_event_t *e)
{
    (void)e;
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

    if (x > 0) {
        lv_timer_reset(s_auto_return_timer);
        lv_timer_resume(s_auto_return_timer);
    } else {
        lv_timer_pause(s_auto_return_timer);
    }
}

/* ====================================================
 *  PAGE 1 : RESUME  (identique a battery_screen)
 * ==================================================== */
static void create_page_resume(lv_obj_t *parent)
{
    setup_tile(parent);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    /* Status bar */
    lv_obj_t *sb = lv_obj_create(parent);
    lv_obj_set_size(sb, SCREEN_W, 22);
    lv_obj_set_pos(sb, 0, 0);
    lv_obj_set_style_bg_color(sb, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_90, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    lv_obj_set_style_pad_hor(sb, 12, 0);
    lv_obj_set_style_pad_ver(sb, 2, 0);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);

    bat_unp_ui.lbl_local_batt = bat_make_label(sb, "-- %", BAT_COL_ACCENT, &lv_font_montserrat_16);
    bat_unp_ui.resume.lbl_state = bat_make_label(sb, "CHARGE", BAT_COL_GREEN, &lv_font_montserrat_16);
    lv_obj_align(bat_unp_ui.resume.lbl_state, LV_ALIGN_RIGHT_MID, 0, 0);

    /* SOC arc */
    bat_unp_ui.resume.arc_soc = lv_arc_create(parent);
    lv_obj_set_size(bat_unp_ui.resume.arc_soc, 390, 390);
    lv_obj_set_pos(bat_unp_ui.resume.arc_soc, (SCREEN_W - 390) / 2, 10);
    lv_arc_set_rotation(bat_unp_ui.resume.arc_soc, 180);
    lv_arc_set_bg_angles(bat_unp_ui.resume.arc_soc, 0, 180);
    lv_arc_set_range(bat_unp_ui.resume.arc_soc, 0, 100);
    lv_arc_set_value(bat_unp_ui.resume.arc_soc, 87);
    lv_obj_remove_style(bat_unp_ui.resume.arc_soc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(bat_unp_ui.resume.arc_soc, 27, LV_PART_MAIN);
    lv_obj_set_style_arc_color(bat_unp_ui.resume.arc_soc, BAT_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bat_unp_ui.resume.arc_soc, 27, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(bat_unp_ui.resume.arc_soc, BAT_COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(bat_unp_ui.resume.arc_soc, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(bat_unp_ui.resume.arc_soc, LV_OBJ_FLAG_CLICKABLE);

    /* SOC % */
    bat_unp_ui.resume.lbl_soc = bat_make_label(parent, "87", BAT_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_set_pos(bat_unp_ui.resume.lbl_soc, 0, 105);
    lv_obj_set_width(bat_unp_ui.resume.lbl_soc, SCREEN_W);
    lv_obj_set_style_text_align(bat_unp_ui.resume.lbl_soc, LV_TEXT_ALIGN_CENTER, 0);

    bat_unp_ui.resume.lbl_soc_unit = bat_make_label(parent, "%", BAT_COL_TEXT_DIM, &lv_font_montserrat_28);
    lv_obj_align_to(bat_unp_ui.resume.lbl_soc_unit, bat_unp_ui.resume.lbl_soc,
                    LV_ALIGN_OUT_RIGHT_TOP, -174, 4);

    /* Power */
    bat_unp_ui.resume.lbl_pow_val = bat_make_label(parent, "186 W", BAT_COL_BLUE, &lv_font_montserrat_48);
    lv_obj_set_pos(bat_unp_ui.resume.lbl_pow_val, 0, 157);
    lv_obj_set_width(bat_unp_ui.resume.lbl_pow_val, SCREEN_W);
    lv_obj_set_style_text_align(bat_unp_ui.resume.lbl_pow_val, LV_TEXT_ALIGN_CENTER, 0);

    /* Scrollable cards: Tension */
    lv_obj_t *c_volt = lv_obj_create(parent);
    lv_obj_set_size(c_volt, SCREEN_W - 16, 56);
    lv_obj_set_pos(c_volt, 8, SCREEN_H + 10);
    bat_set_card_style(c_volt);
    lv_obj_set_style_pad_hor(c_volt, 12, 0);
    lv_obj_set_style_pad_ver(c_volt, 8, 0);
    lv_obj_clear_flag(c_volt, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_volt, "TENSION", BAT_COL_TEXT_DIM, &lv_font_montserrat_36);
    bat_unp_ui.resume.lbl_volt_val = bat_make_label(c_volt, "35.84 V", BAT_COL_TEXT, &lv_font_montserrat_36);
    lv_obj_align(bat_unp_ui.resume.lbl_volt_val, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Scrollable cards: Courant */
    lv_obj_t *c_curr = lv_obj_create(parent);
    lv_obj_set_size(c_curr, SCREEN_W - 16, 56);
    lv_obj_set_pos(c_curr, 8, SCREEN_H + 74);
    bat_set_card_style(c_curr);
    lv_obj_set_style_pad_hor(c_curr, 12, 0);
    lv_obj_set_style_pad_ver(c_curr, 8, 0);
    lv_obj_clear_flag(c_curr, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_curr, "COURANT", BAT_COL_TEXT_DIM, &lv_font_montserrat_36);
    bat_unp_ui.resume.lbl_curr_val = bat_make_label(c_curr, "+5.2 A", BAT_COL_GREEN, &lv_font_montserrat_36);
    lv_obj_align(bat_unp_ui.resume.lbl_curr_val, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ====================================================
 *  PAGE 2 : TENSION PACK  (identique a battery_screen)
 * ==================================================== */
static void create_page_voltage(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Summary: 3 cards */
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
        &bat_unp_ui.volt.lbl_total,
        &bat_unp_ui.volt.lbl_avg,
        &bat_unp_ui.volt.lbl_delta
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

    /* SOC Chart */
    lv_obj_t *chart_card = lv_obj_create(parent);
    lv_obj_set_size(chart_card, 288, 176);
    lv_obj_align(chart_card, LV_ALIGN_TOP_LEFT, 10, 92);
    bat_set_card_style(chart_card);
    lv_obj_set_style_pad_all(chart_card, 6, 0);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(chart_card, "SOC  -  2 derniers cycles", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);

    bat_unp_ui.volt.chart = lv_chart_create(chart_card);
    lv_obj_set_size(bat_unp_ui.volt.chart, 268, 142);
    lv_obj_align(bat_unp_ui.volt.chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(bat_unp_ui.volt.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(bat_unp_ui.volt.chart, SOC_GRAPH_POINTS);
    lv_chart_set_range(bat_unp_ui.volt.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(bat_unp_ui.volt.chart, 4, 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.volt.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_unp_ui.volt.chart, 1, 0);
    lv_obj_set_style_border_color(bat_unp_ui.volt.chart, lv_color_hex(0x333333), 0);
    lv_obj_set_style_line_color(bat_unp_ui.volt.chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(bat_unp_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(bat_unp_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_width(bat_unp_ui.volt.chart, 3, LV_PART_INDICATOR);
    lv_obj_set_style_height(bat_unp_ui.volt.chart, 3, LV_PART_INDICATOR);

    bat_unp_ui.volt.ser_soc = lv_chart_add_series(bat_unp_ui.volt.chart,
                                                    BAT_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(bat_unp_ui.volt.chart, 2, LV_PART_ITEMS);

    /* Pre-fill simulated SOC curve */
    for (int i = 0; i < SOC_GRAPH_POINTS; i++) {
        int v;
        if (i < SOC_GRAPH_POINTS / 2)
            v = 95 - (i * 150 / SOC_GRAPH_POINTS);
        else {
            int j = i - SOC_GRAPH_POINTS / 2;
            v = 20 + (j * 134 / SOC_GRAPH_POINTS);
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_chart_set_next_value(bat_unp_ui.volt.chart, bat_unp_ui.volt.ser_soc, v);
    }

    /* Cap nominale */
    lv_obj_t *cn = lv_obj_create(parent);
    lv_obj_set_size(cn, 142, 86);
    lv_obj_align(cn, LV_ALIGN_TOP_RIGHT, -10, 92);
    bat_set_card_style(cn);
    lv_obj_set_style_pad_all(cn, 6, 0);
    lv_obj_clear_flag(cn, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cn, "CAP. NOM.", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.volt.lbl_cap_nom_sub = bat_make_label(cn, "96.3%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_nom_sub, LV_ALIGN_LEFT_MID, 0, 0);
    bat_unp_ui.volt.lbl_cap_nom_val = bat_make_label(cn, "38.50Ah", BAT_COL_ACCENT2, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_nom_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Cap dynamique */
    lv_obj_t *cd = lv_obj_create(parent);
    lv_obj_set_size(cd, 142, 86);
    lv_obj_align(cd, LV_ALIGN_TOP_RIGHT, -10, 182);
    bat_set_card_style(cd);
    lv_obj_set_style_pad_all(cd, 6, 0);
    lv_obj_clear_flag(cd, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cd, "CAP. DYN.", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.volt.lbl_cap_dyn_sub = bat_make_label(cd, "90.6%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_dyn_sub, LV_ALIGN_LEFT_MID, 0, 0);
    bat_unp_ui.volt.lbl_cap_dyn_val = bat_make_label(cd, "34.90Ah", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_dyn_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

/* ====================================================
 *  PAGE 3 : TEMPERATURES (Cellule + BMS) + MOSFET
 * ==================================================== */
static void create_page_temp(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Card Cellule : nom + temperature en haut, bar en bas */
    lv_obj_t *cell_card = lv_obj_create(parent);
    lv_obj_set_size(cell_card, 260, 130);
    lv_obj_align(cell_card, LV_ALIGN_TOP_LEFT, 8, 0);
    bat_set_card_style(cell_card);
    lv_obj_clear_flag(cell_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(cell_card, "Cellule", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.temp.lbl_temp_val[0] = bat_make_label(cell_card, "25 C", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_temp_val[0], LV_ALIGN_TOP_RIGHT, 0, 0);

    bat_unp_ui.temp.bar_temp[0] = lv_bar_create(cell_card);
    lv_obj_set_size(bat_unp_ui.temp.bar_temp[0], 230, 10);
    lv_obj_align(bat_unp_ui.temp.bar_temp[0], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bat_unp_ui.temp.bar_temp[0], 0, 70);
    lv_bar_set_value(bat_unp_ui.temp.bar_temp[0], 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[0], BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.bar_temp[0], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[0], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[0], BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[0], 4, LV_PART_INDICATOR);

    /* Card BMS : meme pattern que Cellule */
    lv_obj_t *bms_card = lv_obj_create(parent);
    lv_obj_set_size(bms_card, 260, 130);
    lv_obj_align(bms_card, LV_ALIGN_TOP_LEFT, 8, 134);
    bat_set_card_style(bms_card);
    lv_obj_clear_flag(bms_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(bms_card, "BMS", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.temp.lbl_temp_val[1] = bat_make_label(bms_card, "25 C", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_temp_val[1], LV_ALIGN_TOP_RIGHT, 0, 0);

    bat_unp_ui.temp.bar_temp[1] = lv_bar_create(bms_card);
    lv_obj_set_size(bat_unp_ui.temp.bar_temp[1], 230, 10);
    lv_obj_align(bat_unp_ui.temp.bar_temp[1], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bat_unp_ui.temp.bar_temp[1], 0, 70);
    lv_bar_set_value(bat_unp_ui.temp.bar_temp[1], 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[1], BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.bar_temp[1], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[1], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[1], BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[1], 4, LV_PART_INDICATOR);

    /* MOS CHG */
    lv_coord_t mos_x = 272;
    lv_coord_t mos_w = SCREEN_W - mos_x - 8;

    bat_unp_ui.temp.cont_mos_chg = lv_obj_create(parent);
    lv_obj_set_size(bat_unp_ui.temp.cont_mos_chg, mos_w, 130);
    lv_obj_set_pos(bat_unp_ui.temp.cont_mos_chg, mos_x, 8);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_chg, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.cont_mos_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bat_unp_ui.temp.cont_mos_chg, 2, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(bat_unp_ui.temp.cont_mos_chg, LV_OPA_60, 0);
    lv_obj_set_style_radius(bat_unp_ui.temp.cont_mos_chg, 10, 0);
    lv_obj_clear_flag(bat_unp_ui.temp.cont_mos_chg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chg_lbl = bat_make_label(bat_unp_ui.temp.cont_mos_chg, "CHG", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_align(chg_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_chg = lv_obj_create(bat_unp_ui.temp.cont_mos_chg);
    lv_obj_set_size(dot_chg, 18, 18);
    lv_obj_set_style_radius(dot_chg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_chg, 0, 0);
    lv_obj_set_style_shadow_width(dot_chg, 10, 0);
    lv_obj_set_style_shadow_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_chg, LV_OPA_60, 0);
    lv_obj_align(dot_chg, LV_ALIGN_CENTER, 0, 0);

    bat_unp_ui.temp.lbl_mos_chg_state = bat_make_label(bat_unp_ui.temp.cont_mos_chg, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_mos_chg_state, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* MOS DSC */
    bat_unp_ui.temp.cont_mos_dsc = lv_obj_create(parent);
    lv_obj_set_size(bat_unp_ui.temp.cont_mos_dsc, mos_w, 130);
    lv_obj_set_pos(bat_unp_ui.temp.cont_mos_dsc, mos_x, 144);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_dsc, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.cont_mos_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bat_unp_ui.temp.cont_mos_dsc, 2, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(bat_unp_ui.temp.cont_mos_dsc, LV_OPA_60, 0);
    lv_obj_set_style_radius(bat_unp_ui.temp.cont_mos_dsc, 10, 0);
    lv_obj_clear_flag(bat_unp_ui.temp.cont_mos_dsc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dsc_lbl = bat_make_label(bat_unp_ui.temp.cont_mos_dsc, "DSC", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_align(dsc_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_dsc = lv_obj_create(bat_unp_ui.temp.cont_mos_dsc);
    lv_obj_set_size(dot_dsc, 18, 18);
    lv_obj_set_style_radius(dot_dsc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_dsc, 0, 0);
    lv_obj_set_style_shadow_width(dot_dsc, 10, 0);
    lv_obj_set_style_shadow_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_dsc, LV_OPA_60, 0);
    lv_obj_align(dot_dsc, LV_ALIGN_CENTER, 0, 0);

    bat_unp_ui.temp.lbl_mos_dsc_state = bat_make_label(bat_unp_ui.temp.cont_mos_dsc, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_mos_dsc_state, LV_ALIGN_BOTTOM_MID, 0, -4);
}

/* ====================================================
 *  INIT
 * ==================================================== */
void bat_unp_screen_init(void)
{
    /* Ecran principal */
    memset(&bat_unp_ui, 0, sizeof(bat_unp_ui));
    bat_unp_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bat_unp_ui.screen, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.screen, LV_OPA_COVER, 0);

    /* Tileview horizontal — 3 tuiles */
    bat_unp_ui.tileview = lv_tileview_create(bat_unp_ui.screen);
    lv_obj_set_size(bat_unp_ui.tileview, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(bat_unp_ui.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(bat_unp_ui.tileview, LV_SCROLLBAR_MODE_OFF);

    bat_unp_ui.tile_resume  = lv_tileview_add_tile(bat_unp_ui.tileview, 0, 0, LV_DIR_RIGHT);
    bat_unp_ui.tile_voltage = lv_tileview_add_tile(bat_unp_ui.tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    bat_unp_ui.tile_temp    = lv_tileview_add_tile(bat_unp_ui.tileview, 2, 0, LV_DIR_LEFT);

    create_page_resume(bat_unp_ui.tile_resume);
    create_page_voltage(bat_unp_ui.tile_voltage);
    create_page_temp(bat_unp_ui.tile_temp);

    create_dots(bat_unp_ui.screen);
    lv_obj_add_event_cb(bat_unp_ui.tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bat_unp_ui.screen, touch_activity_cb, LV_EVENT_PRESSED, NULL);

    /* Auto-return timer: 30s, starts paused */
    s_auto_return_timer = lv_timer_create(auto_return_timer_cb, 30000, NULL);
    lv_timer_pause(s_auto_return_timer);

    lv_scr_load(bat_unp_ui.screen);
}

/* ====================================================
 *  PORTRAIT LAYOUT (280x456)
 * ==================================================== */
#define PORT_W  280
#define PORT_H  456

static void create_page_resume_portrait(lv_obj_t *parent)
{
    setup_tile(parent);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    /* Status bar */
    lv_obj_t *sb = lv_obj_create(parent);
    lv_obj_set_size(sb, PORT_W, 22);
    lv_obj_set_pos(sb, 0, 0);
    lv_obj_set_style_bg_color(sb, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_90, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    lv_obj_set_style_pad_hor(sb, 12, 0);
    lv_obj_set_style_pad_ver(sb, 2, 0);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);

    bat_unp_ui.lbl_local_batt = bat_make_label(sb, "-- %", BAT_COL_ACCENT, &lv_font_montserrat_16);
    bat_unp_ui.resume.lbl_state = bat_make_label(sb, "CHARGE", BAT_COL_GREEN, &lv_font_montserrat_16);
    lv_obj_align(bat_unp_ui.resume.lbl_state, LV_ALIGN_RIGHT_MID, 0, 0);

    /* SOC arc — plus petit pour portrait */
    bat_unp_ui.resume.arc_soc = lv_arc_create(parent);
    lv_obj_set_size(bat_unp_ui.resume.arc_soc, 240, 240);
    lv_obj_align(bat_unp_ui.resume.arc_soc, LV_ALIGN_TOP_MID, 0, 25);
    lv_arc_set_rotation(bat_unp_ui.resume.arc_soc, 135);
    lv_arc_set_bg_angles(bat_unp_ui.resume.arc_soc, 0, 270);
    lv_arc_set_range(bat_unp_ui.resume.arc_soc, 0, 100);
    lv_arc_set_value(bat_unp_ui.resume.arc_soc, 87);
    lv_obj_remove_style(bat_unp_ui.resume.arc_soc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(bat_unp_ui.resume.arc_soc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_color(bat_unp_ui.resume.arc_soc, BAT_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bat_unp_ui.resume.arc_soc, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(bat_unp_ui.resume.arc_soc, BAT_COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(bat_unp_ui.resume.arc_soc, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(bat_unp_ui.resume.arc_soc, LV_OBJ_FLAG_CLICKABLE);

    /* SOC % — centre dans l'arc */
    bat_unp_ui.resume.lbl_soc = bat_make_label(parent, "87", BAT_COL_TEXT, &lv_font_montserrat_48);
    lv_obj_align(bat_unp_ui.resume.lbl_soc, LV_ALIGN_TOP_MID, -15, 110);

    bat_unp_ui.resume.lbl_soc_unit = bat_make_label(parent, "%", BAT_COL_TEXT_DIM, &lv_font_montserrat_28);
    lv_obj_align_to(bat_unp_ui.resume.lbl_soc_unit, bat_unp_ui.resume.lbl_soc,
                    LV_ALIGN_OUT_RIGHT_TOP, 4, 4);

    /* Power */
    bat_unp_ui.resume.lbl_pow_val = bat_make_label(parent, "186 W", BAT_COL_BLUE, &lv_font_montserrat_36);
    lv_obj_set_width(bat_unp_ui.resume.lbl_pow_val, PORT_W);
    lv_obj_set_style_text_align(bat_unp_ui.resume.lbl_pow_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(bat_unp_ui.resume.lbl_pow_val, LV_ALIGN_TOP_MID, 0, 270);

    /* Card Tension */
    lv_obj_t *c_volt = lv_obj_create(parent);
    lv_obj_set_size(c_volt, PORT_W - 16, 52);
    lv_obj_set_pos(c_volt, 8, 320);
    bat_set_card_style(c_volt);
    lv_obj_set_style_pad_hor(c_volt, 12, 0);
    lv_obj_set_style_pad_ver(c_volt, 8, 0);
    lv_obj_clear_flag(c_volt, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_volt, "TENSION", BAT_COL_TEXT_DIM, &lv_font_montserrat_28);
    bat_unp_ui.resume.lbl_volt_val = bat_make_label(c_volt, "35.84 V", BAT_COL_TEXT, &lv_font_montserrat_28);
    lv_obj_align(bat_unp_ui.resume.lbl_volt_val, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Card Courant */
    lv_obj_t *c_curr = lv_obj_create(parent);
    lv_obj_set_size(c_curr, PORT_W - 16, 52);
    lv_obj_set_pos(c_curr, 8, 380);
    bat_set_card_style(c_curr);
    lv_obj_set_style_pad_hor(c_curr, 12, 0);
    lv_obj_set_style_pad_ver(c_curr, 8, 0);
    lv_obj_clear_flag(c_curr, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(c_curr, "COURANT", BAT_COL_TEXT_DIM, &lv_font_montserrat_28);
    bat_unp_ui.resume.lbl_curr_val = bat_make_label(c_curr, "+5.2 A", BAT_COL_GREEN, &lv_font_montserrat_28);
    lv_obj_align(bat_unp_ui.resume.lbl_curr_val, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_page_voltage_portrait(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Summary: 3 cards empilees verticalement */
    lv_obj_t *sum_row = lv_obj_create(parent);
    lv_obj_set_size(sum_row, PORT_W - 16, 86);
    lv_obj_align(sum_row, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_opa(sum_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sum_row, 0, 0);
    lv_obj_set_style_pad_all(sum_row, 0, 0);
    lv_obj_set_flex_flow(sum_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sum_row, 4, 0);
    lv_obj_clear_flag(sum_row, LV_OBJ_FLAG_SCROLLABLE);

    const char *sum_titles[] = {"TOTALE", "MOY.", "DELTA"};
    lv_obj_t **sum_lbls[] = {
        &bat_unp_ui.volt.lbl_total,
        &bat_unp_ui.volt.lbl_avg,
        &bat_unp_ui.volt.lbl_delta
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
        bat_make_label(c, sum_titles[i], BAT_COL_TEXT_DIM, &lv_font_montserrat_16);
        *sum_lbls[i] = bat_make_label(c, sum_vals[i], sum_cols[i], &lv_font_montserrat_20);
        lv_obj_align(*sum_lbls[i], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    /* SOC Chart — pleine largeur */
    lv_obj_t *chart_card = lv_obj_create(parent);
    lv_obj_set_size(chart_card, PORT_W - 16, 210);
    lv_obj_align(chart_card, LV_ALIGN_TOP_MID, 0, 92);
    bat_set_card_style(chart_card);
    lv_obj_set_style_pad_all(chart_card, 6, 0);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(chart_card, "SOC  -  2 derniers cycles", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);

    bat_unp_ui.volt.chart = lv_chart_create(chart_card);
    lv_obj_set_size(bat_unp_ui.volt.chart, PORT_W - 36, 176);
    lv_obj_align(bat_unp_ui.volt.chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(bat_unp_ui.volt.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(bat_unp_ui.volt.chart, SOC_GRAPH_POINTS);
    lv_chart_set_range(bat_unp_ui.volt.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(bat_unp_ui.volt.chart, 4, 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.volt.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_unp_ui.volt.chart, 1, 0);
    lv_obj_set_style_border_color(bat_unp_ui.volt.chart, lv_color_hex(0x333333), 0);
    lv_obj_set_style_line_color(bat_unp_ui.volt.chart, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(bat_unp_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(bat_unp_ui.volt.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_width(bat_unp_ui.volt.chart, 3, LV_PART_INDICATOR);
    lv_obj_set_style_height(bat_unp_ui.volt.chart, 3, LV_PART_INDICATOR);

    bat_unp_ui.volt.ser_soc = lv_chart_add_series(bat_unp_ui.volt.chart,
                                                    BAT_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(bat_unp_ui.volt.chart, 2, LV_PART_ITEMS);

    for (int i = 0; i < SOC_GRAPH_POINTS; i++) {
        int v;
        if (i < SOC_GRAPH_POINTS / 2)
            v = 95 - (i * 150 / SOC_GRAPH_POINTS);
        else {
            int j = i - SOC_GRAPH_POINTS / 2;
            v = 20 + (j * 134 / SOC_GRAPH_POINTS);
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_chart_set_next_value(bat_unp_ui.volt.chart, bat_unp_ui.volt.ser_soc, v);
    }

    /* Cap cards cote a cote en bas */
    lv_obj_t *cn = lv_obj_create(parent);
    lv_obj_set_size(cn, (PORT_W - 20) / 2, 140);
    lv_obj_set_pos(cn, 8, 306);
    bat_set_card_style(cn);
    lv_obj_set_style_pad_all(cn, 6, 0);
    lv_obj_clear_flag(cn, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cn, "CAP. NOM.", BAT_COL_TEXT_DIM, &lv_font_montserrat_16);
    bat_unp_ui.volt.lbl_cap_nom_sub = bat_make_label(cn, "96.3%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_nom_sub, LV_ALIGN_LEFT_MID, 0, 0);
    bat_unp_ui.volt.lbl_cap_nom_val = bat_make_label(cn, "38.50Ah", BAT_COL_ACCENT2, &lv_font_montserrat_20);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_nom_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *cd = lv_obj_create(parent);
    lv_obj_set_size(cd, (PORT_W - 20) / 2, 140);
    lv_obj_set_pos(cd, PORT_W / 2 + 2, 306);
    bat_set_card_style(cd);
    lv_obj_set_style_pad_all(cd, 6, 0);
    lv_obj_clear_flag(cd, LV_OBJ_FLAG_SCROLLABLE);
    bat_make_label(cd, "CAP. DYN.", BAT_COL_TEXT_DIM, &lv_font_montserrat_16);
    bat_unp_ui.volt.lbl_cap_dyn_sub = bat_make_label(cd, "90.6%", BAT_COL_TEXT_DIM, &lv_font_montserrat_14);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_dyn_sub, LV_ALIGN_LEFT_MID, 0, 0);
    bat_unp_ui.volt.lbl_cap_dyn_val = bat_make_label(cd, "34.90Ah", BAT_COL_ACCENT, &lv_font_montserrat_20);
    lv_obj_align(bat_unp_ui.volt.lbl_cap_dyn_val, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void create_page_temp_portrait(lv_obj_t *parent)
{
    setup_tile(parent);

    /* Card Cellule */
    lv_obj_t *cell_card = lv_obj_create(parent);
    lv_obj_set_size(cell_card, PORT_W - 16, 120);
    lv_obj_align(cell_card, LV_ALIGN_TOP_MID, 0, 2);
    bat_set_card_style(cell_card);
    lv_obj_clear_flag(cell_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(cell_card, "Cellule", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.temp.lbl_temp_val[0] = bat_make_label(cell_card, "25 C", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_temp_val[0], LV_ALIGN_TOP_RIGHT, 0, 0);

    bat_unp_ui.temp.bar_temp[0] = lv_bar_create(cell_card);
    lv_obj_set_size(bat_unp_ui.temp.bar_temp[0], PORT_W - 48, 10);
    lv_obj_align(bat_unp_ui.temp.bar_temp[0], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bat_unp_ui.temp.bar_temp[0], 0, 70);
    lv_bar_set_value(bat_unp_ui.temp.bar_temp[0], 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[0], BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.bar_temp[0], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[0], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[0], BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[0], 4, LV_PART_INDICATOR);

    /* Card BMS */
    lv_obj_t *bms_card = lv_obj_create(parent);
    lv_obj_set_size(bms_card, PORT_W - 16, 120);
    lv_obj_align(bms_card, LV_ALIGN_TOP_MID, 0, 126);
    bat_set_card_style(bms_card);
    lv_obj_clear_flag(bms_card, LV_OBJ_FLAG_SCROLLABLE);

    bat_make_label(bms_card, "BMS", BAT_COL_TEXT_DIM, &lv_font_montserrat_20);
    bat_unp_ui.temp.lbl_temp_val[1] = bat_make_label(bms_card, "25 C", BAT_COL_ACCENT, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_temp_val[1], LV_ALIGN_TOP_RIGHT, 0, 0);

    bat_unp_ui.temp.bar_temp[1] = lv_bar_create(bms_card);
    lv_obj_set_size(bat_unp_ui.temp.bar_temp[1], PORT_W - 48, 10);
    lv_obj_align(bat_unp_ui.temp.bar_temp[1], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bat_unp_ui.temp.bar_temp[1], 0, 70);
    lv_bar_set_value(bat_unp_ui.temp.bar_temp[1], 25, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[1], BAT_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.bar_temp[1], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[1], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[1], BAT_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bat_unp_ui.temp.bar_temp[1], 4, LV_PART_INDICATOR);

    /* MOS CHG + DSC cote a cote */
    lv_coord_t mos_w = (PORT_W - 24) / 2;

    bat_unp_ui.temp.cont_mos_chg = lv_obj_create(parent);
    lv_obj_set_size(bat_unp_ui.temp.cont_mos_chg, mos_w, 190);
    lv_obj_set_pos(bat_unp_ui.temp.cont_mos_chg, 8, 252);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_chg, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.cont_mos_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bat_unp_ui.temp.cont_mos_chg, 2, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(bat_unp_ui.temp.cont_mos_chg, LV_OPA_60, 0);
    lv_obj_set_style_radius(bat_unp_ui.temp.cont_mos_chg, 10, 0);
    lv_obj_clear_flag(bat_unp_ui.temp.cont_mos_chg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *chg_lbl = bat_make_label(bat_unp_ui.temp.cont_mos_chg, "CHG", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_align(chg_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_chg = lv_obj_create(bat_unp_ui.temp.cont_mos_chg);
    lv_obj_set_size(dot_chg, 18, 18);
    lv_obj_set_style_radius(dot_chg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_chg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_chg, 0, 0);
    lv_obj_set_style_shadow_width(dot_chg, 10, 0);
    lv_obj_set_style_shadow_color(dot_chg, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_chg, LV_OPA_60, 0);
    lv_obj_align(dot_chg, LV_ALIGN_CENTER, 0, 0);

    bat_unp_ui.temp.lbl_mos_chg_state = bat_make_label(bat_unp_ui.temp.cont_mos_chg, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_mos_chg_state, LV_ALIGN_BOTTOM_MID, 0, -4);

    bat_unp_ui.temp.cont_mos_dsc = lv_obj_create(parent);
    lv_obj_set_size(bat_unp_ui.temp.cont_mos_dsc, mos_w, 190);
    lv_obj_set_pos(bat_unp_ui.temp.cont_mos_dsc, PORT_W / 2 + 4, 252);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_dsc, lv_color_hex(0x0F1A0F), 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.temp.cont_mos_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bat_unp_ui.temp.cont_mos_dsc, 2, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_border_opa(bat_unp_ui.temp.cont_mos_dsc, LV_OPA_60, 0);
    lv_obj_set_style_radius(bat_unp_ui.temp.cont_mos_dsc, 10, 0);
    lv_obj_clear_flag(bat_unp_ui.temp.cont_mos_dsc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dsc_lbl = bat_make_label(bat_unp_ui.temp.cont_mos_dsc, "DSC", BAT_COL_GREEN, &lv_font_montserrat_20);
    lv_obj_align(dsc_lbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *dot_dsc = lv_obj_create(bat_unp_ui.temp.cont_mos_dsc);
    lv_obj_set_size(dot_dsc, 18, 18);
    lv_obj_set_style_radius(dot_dsc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_bg_opa(dot_dsc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_dsc, 0, 0);
    lv_obj_set_style_shadow_width(dot_dsc, 10, 0);
    lv_obj_set_style_shadow_color(dot_dsc, BAT_COL_GREEN, 0);
    lv_obj_set_style_shadow_opa(dot_dsc, LV_OPA_60, 0);
    lv_obj_align(dot_dsc, LV_ALIGN_CENTER, 0, 0);

    bat_unp_ui.temp.lbl_mos_dsc_state = bat_make_label(bat_unp_ui.temp.cont_mos_dsc, "ON", BAT_COL_GREEN, &lv_font_montserrat_22);
    lv_obj_align(bat_unp_ui.temp.lbl_mos_dsc_state, LV_ALIGN_BOTTOM_MID, 0, -4);
}

/* ====================================================
 *  INIT PORTRAIT (280x456)
 * ==================================================== */
void bat_unp_screen_init_portrait(void)
{
    memset(&bat_unp_ui, 0, sizeof(bat_unp_ui));
    bat_unp_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bat_unp_ui.screen, BAT_COL_BG, 0);
    lv_obj_set_style_bg_opa(bat_unp_ui.screen, LV_OPA_COVER, 0);

    bat_unp_ui.tileview = lv_tileview_create(bat_unp_ui.screen);
    lv_obj_set_size(bat_unp_ui.tileview, PORT_W, PORT_H);
    lv_obj_set_style_bg_opa(bat_unp_ui.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(bat_unp_ui.tileview, LV_SCROLLBAR_MODE_OFF);

    bat_unp_ui.tile_resume  = lv_tileview_add_tile(bat_unp_ui.tileview, 0, 0, LV_DIR_RIGHT);
    bat_unp_ui.tile_voltage = lv_tileview_add_tile(bat_unp_ui.tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    bat_unp_ui.tile_temp    = lv_tileview_add_tile(bat_unp_ui.tileview, 2, 0, LV_DIR_LEFT);

    create_page_resume_portrait(bat_unp_ui.tile_resume);
    create_page_voltage_portrait(bat_unp_ui.tile_voltage);
    create_page_temp_portrait(bat_unp_ui.tile_temp);

    create_dots(bat_unp_ui.screen);
    lv_obj_add_event_cb(bat_unp_ui.tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bat_unp_ui.screen, touch_activity_cb, LV_EVENT_PRESSED, NULL);

    s_auto_return_timer = lv_timer_create(auto_return_timer_cb, 30000, NULL);
    lv_timer_pause(s_auto_return_timer);

    lv_scr_load(bat_unp_ui.screen);
}

/* ====================================================
 *  CLEANUP
 * ==================================================== */
void bat_unp_screen_cleanup(void)
{
    if (s_auto_return_timer) {
        lv_timer_del(s_auto_return_timer);
        s_auto_return_timer = NULL;
    }
}

/* ====================================================
 *  UPDATE
 * ==================================================== */
void bat_unp_screen_update(void)
{
    char buf[32];

    /* Page 1 : Resume */
    lv_label_set_text(bat_unp_ui.resume.lbl_state, state_names[bms_data.state]);
    lv_obj_set_style_text_color(bat_unp_ui.resume.lbl_state, state_color(bms_data.state), 0);

    lv_arc_set_value(bat_unp_ui.resume.arc_soc, bms_data.soc);
    lv_obj_set_style_arc_color(bat_unp_ui.resume.arc_soc, soc_color(bms_data.soc), LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%d", bms_data.soc);
    lv_label_set_text(bat_unp_ui.resume.lbl_soc, buf);

    snprintf(buf, sizeof(buf), "%.2f V", bms_data.voltage);
    lv_label_set_text(bat_unp_ui.resume.lbl_volt_val, buf);

    snprintf(buf, sizeof(buf), "%+.1f A", bms_data.current);
    lv_label_set_text(bat_unp_ui.resume.lbl_curr_val, buf);
    lv_obj_set_style_text_color(bat_unp_ui.resume.lbl_curr_val,
        bms_data.current > 0 ? BAT_COL_GREEN :
        bms_data.current < 0 ? BAT_COL_AMBER : BAT_COL_TEXT_DIM, 0);

    snprintf(buf, sizeof(buf), "%.0f W", bms_data.power);
    lv_label_set_text(bat_unp_ui.resume.lbl_pow_val, buf);
    lv_obj_set_style_text_color(bat_unp_ui.resume.lbl_pow_val,
        (bms_data.state == BMS_STATE_CHARGE) ? BAT_COL_GREEN : BAT_COL_BLUE, 0);

    /* Page 2 : Tension */
    snprintf(buf, sizeof(buf), "%.2fV", bms_data.voltage);
    lv_label_set_text(bat_unp_ui.volt.lbl_total, buf);
    snprintf(buf, sizeof(buf), "%.3fV", bms_data.cell_avg);
    lv_label_set_text(bat_unp_ui.volt.lbl_avg, buf);
    snprintf(buf, sizeof(buf), "%dmV", bms_data.cell_delta);
    lv_label_set_text(bat_unp_ui.volt.lbl_delta, buf);

    snprintf(buf, sizeof(buf), "%.2fAh", bms_data.cap_nominal / 1000.0f);
    lv_label_set_text(bat_unp_ui.volt.lbl_cap_nom_val, buf);
    if (bms_data.cap_origin > 0) {
        snprintf(buf, sizeof(buf), "%.1f%%", bms_data.cap_nominal * 100.0f / bms_data.cap_origin);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(bat_unp_ui.volt.lbl_cap_nom_sub, buf);

    snprintf(buf, sizeof(buf), "%.2fAh", bms_data.cap_dynamic / 1000.0f);
    lv_label_set_text(bat_unp_ui.volt.lbl_cap_dyn_val, buf);
    if (bms_data.cap_nominal > 0) {
        snprintf(buf, sizeof(buf), "%.1f%%", bms_data.cap_dynamic * 100.0f / bms_data.cap_nominal);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(bat_unp_ui.volt.lbl_cap_dyn_sub, buf);

    /* Page 3 : Temperatures (Cellule + BMS) + MOS */
    float temps[2] = { bms_data.temp_cell, bms_data.temp_bms };
    for (int i = 0; i < 2; i++) {
        lv_bar_set_value(bat_unp_ui.temp.bar_temp[i], (int32_t)temps[i], LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "%.1f C", temps[i]);
        lv_label_set_text(bat_unp_ui.temp.lbl_temp_val[i], buf);
        lv_color_t tc = temp_color(temps[i]);
        lv_obj_set_style_text_color(bat_unp_ui.temp.lbl_temp_val[i], tc, 0);
        lv_obj_set_style_bg_color(bat_unp_ui.temp.bar_temp[i], tc, LV_PART_INDICATOR);
    }

    /* MOS CHG */
    lv_color_t chg_col = bms_data.mos_chg ? BAT_COL_GREEN : BAT_COL_RED;
    lv_label_set_text(bat_unp_ui.temp.lbl_mos_chg_state, bms_data.mos_chg ? "ON" : "OFF");
    lv_obj_set_style_text_color(bat_unp_ui.temp.lbl_mos_chg_state, chg_col, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_chg, chg_col, 0);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_chg,
        bms_data.mos_chg ? lv_color_hex(0x0F1A0F) : lv_color_hex(0x1A0F0F), 0);

    /* MOS DSC */
    lv_color_t dsc_col = bms_data.mos_dsc ? BAT_COL_GREEN : BAT_COL_RED;
    lv_label_set_text(bat_unp_ui.temp.lbl_mos_dsc_state, bms_data.mos_dsc ? "ON" : "OFF");
    lv_obj_set_style_text_color(bat_unp_ui.temp.lbl_mos_dsc_state, dsc_col, 0);
    lv_obj_set_style_border_color(bat_unp_ui.temp.cont_mos_dsc, dsc_col, 0);
    lv_obj_set_style_bg_color(bat_unp_ui.temp.cont_mos_dsc,
        bms_data.mos_dsc ? lv_color_hex(0x0F1A0F) : lv_color_hex(0x1A0F0F), 0);
}
