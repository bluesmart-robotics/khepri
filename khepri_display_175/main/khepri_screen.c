/*
 * khepri_screen.c — Écran pilote KHÉPRI (LVGL 8.3, AMOLED rond 466×466).
 * Voir khepri_screen.h.
 */
#include "khepri_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Textes d'affichage : une troncature éventuelle est acceptable */
#pragma GCC diagnostic ignored "-Wformat-truncation"

LV_FONT_DECLARE(kh_font_14);
LV_FONT_DECLARE(kh_font_18);
LV_FONT_DECLARE(kh_font_24);
LV_FONT_DECLARE(kh_font_32);
LV_FONT_DECLARE(kh_font_64);

#define RPM_MAX 4000
#define G_MAX 1200
#define VIB_MAX 8000

static const char *PAGE_TITLE[KH_PAGE_COUNT] = {"MOTEURS", "SOLAIRE", "VIBRATIONS", "SYSTÈME"};

static kh_screen_cb_t s_cb;

static struct {
    lv_obj_t *scr, *tv, *tile[KH_PAGE_COUNT];
    lv_obj_t *dots[KH_PAGE_COUNT];
    lv_obj_t *status;   /* liaison / simulation, en haut */
    lv_obj_t *knob_arc; /* indicateur de molette sur le pourtour */

    struct {
        lv_obj_t *arc[2], *val[2], *tag[2], *unit, *vib[2];
    } mot;
    struct {
        lv_obj_t *arc[2], *g, *unit, *isc[2], *tc[2], *air, *hdr[2];
    } sol;
    struct {
        lv_obj_t *bar[2], *val[2], *ord[2], *peak;
    } vib;
    struct {
        lv_obj_t *link, *link2, *gps, *node[2], *btn_bind, *lbl_bind, *btn_mark, *btn_demo, *lbl_demo, *btn_lum,
            *lbl_lum, *mac;
    } sys;
    struct {
        lv_obj_t *root, *ring, *title, *node, *val, *rpm, *hint;
    } al;

    bool demo, bound;
    int lum;
} ui;

/* ------------------------------------------------------------------ formats français */
static void fmt_int(char *b, size_t n, long v)
{
    char tmp[24];
    bool neg = v < 0;
    unsigned long u = (unsigned long)(neg ? -v : v);
    int len = snprintf(tmp, sizeof tmp, "%lu", u);
    char *p = b;
    size_t left = n;
    if (neg && left > 1) { *p++ = '-'; left--; }
    for (int i = 0; i < len && left > 1; i++) {
        *p++ = tmp[i];
        left--;
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0 && left > 1) { *p++ = ' '; left--; }
    }
    *p = 0;
}

static void fmt_dec(char *b, size_t n, float v, int dec)
{
    snprintf(b, n, "%.*f", dec, (double)v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
}

/* ------------------------------------------------------------------ fabrique de widgets */
static lv_obj_t *label(lv_obj_t *par, const lv_font_t *f, lv_color_t c, const char *txt)
{
    lv_obj_t *l = lv_label_create(par);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *gauge_arc(lv_obj_t *par, int size, int width, lv_color_t c, int max)
{
    lv_obj_t *a = lv_arc_create(par);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, 135, 45);
    lv_arc_set_range(a, 0, max);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, KH_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}

/* Demi-anneau sur le pourtour : gauche (rempli de bas en haut, sens horaire) ou droite (miroir).
 * Les deux moitiés laissent libres le haut (titre) et le bas (pastilles de page). */
static lv_obj_t *half_arc(lv_obj_t *par, int size, int width, lv_color_t c, int max, bool left)
{
    lv_obj_t *a = gauge_arc(par, size, width, c, max);
    if (left) {
        lv_arc_set_bg_angles(a, 110, 246);
    } else {
        lv_arc_set_bg_angles(a, 294, 70);
        lv_arc_set_mode(a, LV_ARC_MODE_REVERSE);
    }
    return a;
}

static void tile_base(lv_obj_t *t, kh_page_t p)
{
    lv_obj_set_style_bg_color(t, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(t, &kh_font_18, KH_COL_TEXT_DIM, PAGE_TITLE[p]);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
}

static void btn_cb(lv_event_t *e);

static lv_obj_t *button(lv_obj_t *par, const char *txt, int w, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_set_size(b, w, 50);
    lv_obj_set_style_radius(b, 25, 0);
    lv_obj_set_style_bg_color(b, KH_COL_CARD2, 0);
    lv_obj_set_style_bg_color(b, KH_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = label(b, &kh_font_18, KH_COL_TEXT, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
    if (lbl_out) *lbl_out = l;
    return b;
}

/* ------------------------------------------------------------------ pages */
static void build_motors(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_MOTORS);
    ui.mot.arc[0] = half_arc(t, 430, 22, KH_COL_ACCENT, RPM_MAX, true);
    ui.mot.arc[1] = half_arc(t, 430, 22, KH_COL_BLUE, RPM_MAX, false);
    for (int k = 0; k < 2; k++) {
        ui.mot.val[k] = label(t, &kh_font_64, KH_COL_TEXT, "---");
        lv_obj_align(ui.mot.val[k], LV_ALIGN_CENTER, 12, k == 0 ? -46 : 30);
        ui.mot.tag[k] = label(t, &kh_font_18, k == 0 ? KH_COL_ACCENT : KH_COL_BLUE, k == 0 ? "M0" : "M1");
        lv_obj_align_to(ui.mot.tag[k], ui.mot.val[k], LV_ALIGN_OUT_LEFT_MID, -10, 0);
    }
    ui.mot.unit = label(t, &kh_font_18, KH_COL_TEXT_DIM, "tr/min");
    lv_obj_align(ui.mot.unit, LV_ALIGN_CENTER, 0, 86);
    for (int k = 0; k < 2; k++) {
        ui.mot.vib[k] = label(t, &kh_font_18, KH_COL_TEXT_DIM, "");
        lv_obj_align(ui.mot.vib[k], LV_ALIGN_CENTER, k == 0 ? -66 : 66, 140);
    }
}

static void build_solar(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_SOLAR);
    ui.sol.arc[0] = half_arc(t, 430, 22, KH_COL_SUN, G_MAX, true);
    ui.sol.arc[1] = half_arc(t, 430, 22, KH_COL_SUN, G_MAX, false);
    ui.sol.g = label(t, &kh_font_64, KH_COL_TEXT, "---");
    lv_obj_align(ui.sol.g, LV_ALIGN_CENTER, 0, -72);
    ui.sol.unit = label(t, &kh_font_18, KH_COL_TEXT_DIM, "W/m² moyen");
    lv_obj_align(ui.sol.unit, LV_ALIGN_CENTER, 0, -22);
    for (int k = 0; k < 2; k++) {
        int x = k == 0 ? -72 : 72;
        ui.sol.hdr[k] = label(t, &kh_font_14, KH_COL_TEXT_DIM, k == 0 ? "CH. 0" : "CH. 1");
        lv_obj_align(ui.sol.hdr[k], LV_ALIGN_CENTER, x, 18);
        ui.sol.isc[k] = label(t, &kh_font_24, KH_COL_TEXT, "---");
        lv_obj_align(ui.sol.isc[k], LV_ALIGN_CENTER, x, 50);
        ui.sol.tc[k] = label(t, &kh_font_24, KH_COL_TEXT_MID, "---");
        lv_obj_align(ui.sol.tc[k], LV_ALIGN_CENTER, x, 86);
    }
    lv_obj_t *sep = lv_obj_create(t);
    lv_obj_set_size(sep, 2, 96);
    lv_obj_set_style_bg_color(sep, KH_COL_CARD2, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, 62);
    ui.sol.air = label(t, &kh_font_18, KH_COL_TEXT_DIM, "Air ---");
    lv_obj_align(ui.sol.air, LV_ALIGN_CENTER, 0, 140);
}

static void build_vib(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_VIB);
    for (int k = 0; k < 2; k++) {
        int x = k == 0 ? -64 : 64;
        lv_obj_t *h = label(t, &kh_font_18, KH_COL_TEXT_MID, k == 0 ? "Ch. 0" : "Ch. 1");
        lv_obj_align(h, LV_ALIGN_CENTER, x, -128);

        lv_obj_t *b = lv_bar_create(t);
        lv_obj_set_size(b, 52, 200);
        lv_obj_align(b, LV_ALIGN_CENTER, x, -4);
        lv_bar_set_range(b, 0, VIB_MAX);
        lv_obj_set_style_radius(b, 10, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 10, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(b, KH_COL_CARD, LV_PART_MAIN);
        lv_obj_set_style_border_color(b, KH_COL_CARD2, LV_PART_MAIN);
        lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(b, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, KH_COL_GREEN, LV_PART_INDICATOR);
        ui.vib.bar[k] = b;

        /* repères d'avertissement et d'alarme */
        const int thr[2] = {KH_VIB_WARN_MG, KH_VIB_ALARM_MG};
        for (int i = 0; i < 2; i++) {
            lv_obj_t *m = lv_obj_create(t);
            lv_obj_set_size(m, 66, 3);
            lv_obj_set_style_border_width(m, 0, 0);
            lv_obj_set_style_radius(m, 1, 0);
            lv_obj_set_style_bg_color(m, i == 0 ? KH_COL_AMBER : KH_COL_RED, 0);
            lv_obj_align_to(m, b, LV_ALIGN_BOTTOM_MID, 0, -(200 * thr[i] / VIB_MAX));
        }

        ui.vib.val[k] = label(t, &kh_font_24, KH_COL_TEXT, "---");
        lv_obj_align(ui.vib.val[k], LV_ALIGN_CENTER, x, 118);
        ui.vib.ord[k] = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
        lv_obj_align(ui.vib.ord[k], LV_ALIGN_CENTER, x, 146);
    }
    ui.vib.peak = label(t, &kh_font_14, KH_COL_TEXT_DIM, "mg RMS · seuils 3 000 / 6 000");
    lv_obj_align(ui.vib.peak, LV_ALIGN_CENTER, 0, 176);
}

static void build_system(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_SYSTEM);
    const int x0 = -150;
    ui.sys.link = label(t, &kh_font_18, KH_COL_TEXT, "");
    lv_obj_align(ui.sys.link, LV_ALIGN_CENTER, 0, -122);
    ui.sys.link2 = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.sys.link2, LV_ALIGN_CENTER, 0, -98);
    ui.sys.gps = label(t, &kh_font_18, KH_COL_TEXT, "");
    lv_obj_align(ui.sys.gps, LV_ALIGN_CENTER, 0, -66);
    for (int k = 0; k < 2; k++) {
        ui.sys.node[k] = label(t, &kh_font_14, KH_COL_TEXT, "");
        lv_obj_set_width(ui.sys.node[k], 2 * -x0 - 20);
        lv_obj_set_style_text_align(ui.sys.node[k], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(ui.sys.node[k], LV_LABEL_LONG_WRAP);
        lv_obj_align(ui.sys.node[k], LV_ALIGN_CENTER, 0, k == 0 ? -30 : 6);
    }
    ui.sys.btn_bind = button(t, LV_SYMBOL_WIFI " LIER", 138, &ui.sys.lbl_bind);
    lv_obj_align(ui.sys.btn_bind, LV_ALIGN_CENTER, -74, 64);
    ui.sys.btn_mark = button(t, LV_SYMBOL_BELL " REPÈRE", 138, NULL);
    lv_obj_align(ui.sys.btn_mark, LV_ALIGN_CENTER, 74, 64);
    ui.sys.btn_demo = button(t, "SIMU", 112, &ui.sys.lbl_demo);
    lv_obj_align(ui.sys.btn_demo, LV_ALIGN_CENTER, -60, 124);
    ui.sys.btn_lum = button(t, LV_SYMBOL_IMAGE " 100 %", 112, &ui.sys.lbl_lum);
    lv_obj_align(ui.sys.btn_lum, LV_ALIGN_CENTER, 60, 124);
    ui.sys.mac = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.sys.mac, LV_ALIGN_CENTER, 0, 172);
}

/* ------------------------------------------------------------------ alerte */
static void alarm_ring_anim(void *obj, int32_t v) { lv_obj_set_style_arc_opa(obj, (lv_opa_t)v, LV_PART_MAIN); }

static void alarm_cb(lv_event_t *e)
{
    (void)e;
    if (s_cb.on_ack) s_cb.on_ack();
}

static void build_alarm(void)
{
    lv_obj_t *r = lv_obj_create(lv_layer_top());
    lv_obj_set_size(r, KH_SCR_W, KH_SCR_H);
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x140404), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, alarm_cb, LV_EVENT_CLICKED, NULL);
    ui.al.root = r;

    lv_obj_t *ring = lv_arc_create(r);
    lv_obj_set_size(ring, 452, 452);
    lv_obj_center(ring);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_style(ring, NULL, LV_PART_INDICATOR);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, 26, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, KH_COL_RED, LV_PART_MAIN);
    ui.al.ring = ring;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, alarm_ring_anim);
    lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&a, 450);
    lv_anim_set_playback_time(&a, 450);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    ui.al.title = label(r, &kh_font_32, KH_COL_RED, LV_SYMBOL_WARNING " VIBRATIONS");
    lv_obj_align(ui.al.title, LV_ALIGN_CENTER, 0, -112);
    ui.al.node = label(r, &kh_font_24, KH_COL_TEXT, "Chaussette 1");
    lv_obj_align(ui.al.node, LV_ALIGN_CENTER, 0, -66);
    ui.al.val = label(r, &kh_font_64, KH_COL_RED, "--- mg");
    lv_obj_align(ui.al.val, LV_ALIGN_CENTER, 0, -6);
    ui.al.rpm = label(r, &kh_font_18, KH_COL_TEXT_MID, "");
    lv_obj_align(ui.al.rpm, LV_ALIGN_CENTER, 0, 56);
    ui.al.hint = label(r, &kh_font_18, KH_COL_TEXT_DIM, "Toucher ou PWR\npour acquitter");
    lv_obj_set_style_text_align(ui.al.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.al.hint, LV_ALIGN_CENTER, 0, 118);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ éléments communs */
static void tv_cb(lv_event_t *e)
{
    (void)e;
    kh_page_t p = kh_screen_page();
    for (int i = 0; i < KH_PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(ui.dots[i], i == (int)p ? KH_COL_TEXT : KH_COL_CARD2, 0);
        lv_obj_set_width(ui.dots[i], i == (int)p ? 22 : 10);
    }
}

static void build_overlay(void)
{
    lv_obj_t *top = lv_layer_top();

    lv_obj_t *dots = lv_obj_create(ui.scr);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, 130, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 8, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < KH_PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, 5, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, KH_COL_CARD2, 0);
        ui.dots[i] = d;
    }

    ui.status = label(ui.scr, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.status, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *k = lv_arc_create(top);
    lv_obj_set_size(k, 462, 462);
    lv_obj_center(k);
    lv_obj_remove_style(k, NULL, LV_PART_KNOB);
    lv_obj_remove_style(k, NULL, LV_PART_MAIN);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(k, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(k, KH_COL_TEXT_MID, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(k, true, LV_PART_INDICATOR);
    lv_arc_set_mode(k, LV_ARC_MODE_NORMAL);
    lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN);
    ui.knob_arc = k;
}

void kh_screen_init(const kh_screen_cb_t *cb)
{
    memset(&ui, 0, sizeof ui);
    if (cb) s_cb = *cb;
    ui.lum = 100;

    ui.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui.scr, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(ui.scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(ui.scr, &kh_font_18, 0);

    ui.tv = lv_tileview_create(ui.scr);
    lv_obj_set_size(ui.tv, KH_SCR_W, KH_SCR_H);
    lv_obj_set_style_bg_color(ui.tv, KH_COL_BG, 0);
    lv_obj_set_scrollbar_mode(ui.tv, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < KH_PAGE_COUNT; i++) {
        lv_dir_t dir = (i > 0 ? LV_DIR_LEFT : 0) | (i < KH_PAGE_COUNT - 1 ? LV_DIR_RIGHT : 0);
        ui.tile[i] = lv_tileview_add_tile(ui.tv, (uint8_t)i, 0, dir);
    }
    build_motors(ui.tile[KH_PAGE_MOTORS]);
    build_solar(ui.tile[KH_PAGE_SOLAR]);
    build_vib(ui.tile[KH_PAGE_VIB]);
    build_system(ui.tile[KH_PAGE_SYSTEM]);
    build_overlay();
    build_alarm();
    lv_obj_add_event_cb(ui.tv, tv_cb, LV_EVENT_VALUE_CHANGED, NULL);
    tv_cb(NULL);
    lv_scr_load(ui.scr);
}

kh_page_t kh_screen_page(void)
{
    lv_obj_t *act = lv_tileview_get_tile_act(ui.tv);
    for (int i = 0; i < KH_PAGE_COUNT; i++)
        if (act == ui.tile[i]) return (kh_page_t)i;
    return KH_PAGE_MOTORS;
}

void kh_screen_show_page(kh_page_t p, bool anim)
{
    if (p >= KH_PAGE_COUNT) return;
    lv_obj_set_tile_id(ui.tv, (uint32_t)p, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    tv_cb(NULL);
}

void kh_screen_knob_step(int steps)
{
    if (!steps || !lv_obj_has_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN)) return; /* l'alerte n'est pas acquittée à la molette */
    int p = ((int)kh_screen_page() + steps) % KH_PAGE_COUNT;
    if (p < 0) p += KH_PAGE_COUNT;
    kh_screen_show_page((kh_page_t)p, true);
}

void kh_screen_button(void)
{
    if (!lv_obj_has_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN)) {
        if (s_cb.on_ack) s_cb.on_ack();
    } else {
        kh_screen_show_page(KH_PAGE_MOTORS, true);
    }
}

static void btn_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    if (b == ui.sys.btn_bind && s_cb.on_bind) s_cb.on_bind(!ui.bound);
    else if (b == ui.sys.btn_mark && s_cb.on_mark) s_cb.on_mark();
    else if (b == ui.sys.btn_demo && s_cb.on_demo) s_cb.on_demo(!ui.demo);
    else if (b == ui.sys.btn_lum) {
        ui.lum = ui.lum >= 100 ? 60 : ui.lum >= 60 ? 30 : 100;
        if (s_cb.on_brightness) s_cb.on_brightness(ui.lum);
    }
}

/* ------------------------------------------------------------------ mise à jour */
static lv_color_t level_color(int lvl)
{
    return lvl == 2 ? KH_COL_RED : lvl == 1 ? KH_COL_AMBER : lvl == 0 ? KH_COL_GREEN : KH_COL_TEXT_DIM;
}

static void set_arc(lv_obj_t *a, bool ok, int v, lv_color_t c)
{
    lv_arc_set_value(a, ok ? v : 0);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
}

static void update_motors(const kh_model_t *m, int64_t now)
{
    char b[32];
    for (int k = 0; k < 2; k++) {
        bool ok = kh_fresh(m->t_motor[k], now, KH_STALE_MS);
        int rpm = ok ? (int)lroundf(fabsf(m->rpm[k])) : 0;
        if (ok) fmt_int(b, sizeof b, rpm);
        else snprintf(b, sizeof b, "---");
        lv_label_set_text(ui.mot.val[k], b);
        lv_obj_set_style_text_color(ui.mot.val[k], ok ? KH_COL_TEXT : KH_COL_TEXT_DIM, 0);
        lv_obj_align(ui.mot.val[k], LV_ALIGN_CENTER, 12, k == 0 ? -46 : 30);
        lv_obj_align_to(ui.mot.tag[k], ui.mot.val[k], LV_ALIGN_OUT_LEFT_MID, -10, 0);
        set_arc(ui.mot.arc[k], ok, rpm > RPM_MAX ? RPM_MAX : rpm, k == 0 ? KH_COL_ACCENT : KH_COL_BLUE);

        int lvl = kh_vib_level(&m->node[k], now);
        static const char *TXT[] = {"---", "OK", "VIB", "ALARME"};
        snprintf(b, sizeof b, "Ch.%d %s", k, TXT[lvl + 1]);
        lv_label_set_text(ui.mot.vib[k], b);
        lv_obj_set_style_text_color(ui.mot.vib[k], level_color(lvl), 0);
    }
}

static void update_solar(const kh_model_t *m, int64_t now)
{
    char b[48], v[24];
    float gsum = 0, air = NAN;
    int gn = 0;
    for (int k = 0; k < 2; k++) {
        const kh_node_t *n = &m->node[k];
        bool ok = kh_fresh(n->t_pv, now, KH_STALE_MS);
        if (ok && n->pv.isc_ma != KP_I16_INVALID) {
            fmt_dec(v, sizeof v, n->pv.isc_ma / 1000.0f, 2);
            snprintf(b, sizeof b, "%s A", v);
            gsum += n->pv.g_wm2;
            gn++;
            set_arc(ui.sol.arc[k], true, n->pv.g_wm2 > G_MAX ? G_MAX : n->pv.g_wm2, KH_COL_SUN);
        } else {
            snprintf(b, sizeof b, "---");
            set_arc(ui.sol.arc[k], false, 0, KH_COL_SUN);
        }
        lv_label_set_text(ui.sol.isc[k], b);
        if (ok && n->pv.tc_c100 != KP_I16_INVALID) {
            fmt_dec(v, sizeof v, n->pv.tc_c100 / 100.0f, 1);
            snprintf(b, sizeof b, "%s °C", v);
        } else {
            snprintf(b, sizeof b, "--- °C");
        }
        lv_label_set_text(ui.sol.tc[k], b);
        if (ok && n->pv.tair_c100 != KP_I16_INVALID && isnan(air)) air = n->pv.tair_c100 / 100.0f;
    }
    if (gn) {
        int g = (int)lroundf(gsum / gn);
        fmt_int(b, sizeof b, g);
        lv_label_set_text(ui.sol.g, b);
        lv_obj_set_style_text_color(ui.sol.g, KH_COL_TEXT, 0);
    } else {
        lv_label_set_text(ui.sol.g, "---");
        lv_obj_set_style_text_color(ui.sol.g, KH_COL_TEXT_DIM, 0);
    }
    if (!isnan(air)) {
        fmt_dec(v, sizeof v, air, 1);
        snprintf(b, sizeof b, "Air extérieur %s °C", v);
    } else {
        snprintf(b, sizeof b, "Air extérieur --- °C");
    }
    lv_label_set_text(ui.sol.air, b);
}

static void update_vib(const kh_model_t *m, int64_t now)
{
    char b[48], v1[16], v2[16];
    for (int k = 0; k < 2; k++) {
        const kh_node_t *n = &m->node[k];
        bool ok = kh_fresh(n->t_rms, now, KH_STALE_MS);
        float vec = ok ? kh_vib_vec_mg(&n->rms) : NAN;
        int lvl = kh_vib_level(n, now);
        lv_color_t c = level_color(lvl);
        if (!isnan(vec)) {
            int iv = (int)lroundf(vec);
            lv_bar_set_value(ui.vib.bar[k], iv > VIB_MAX ? VIB_MAX : iv, LV_ANIM_OFF);
            fmt_int(v1, sizeof v1, iv);
            snprintf(b, sizeof b, "%s", v1);
        } else {
            lv_bar_set_value(ui.vib.bar[k], 0, LV_ANIM_OFF);
            snprintf(b, sizeof b, "---");
        }
        lv_obj_set_style_bg_color(ui.vib.bar[k], c, LV_PART_INDICATOR);
        lv_label_set_text(ui.vib.val[k], b);
        lv_obj_set_style_text_color(ui.vib.val[k], lvl > 0 ? c : KH_COL_TEXT, 0);
        if (kh_fresh(n->t_order, now, KH_STALE_MS)) {
            fmt_int(v1, sizeof v1, n->order.amp1p_mg[0]);
            fmt_int(v2, sizeof v2, n->order.amp1p_mg[1]);
            snprintf(b, sizeof b, "1P %s · %s", v1, v2);
        } else {
            b[0] = 0;
        }
        lv_label_set_text(ui.vib.ord[k], b);
    }
}

static const char *node_text(const kh_node_t *n, int64_t now, char *b, size_t len, lv_color_t *c)
{
    if (!kh_fresh(n->t_status, now, KH_STALE_MS)) {
        *c = KH_COL_TEXT_DIM;
        snprintf(b, len, "muette");
        return b;
    }
    const kp_status_t *s = &n->status;
    if (s->faults) {
        *c = KH_COL_AMBER;
        snprintf(b, len, "DÉFAUT%s%s%s%s%s%s%s%s", s->faults & KP_F_ACC ? " accéléro" : "",
                 s->faults & KP_F_RTD_A ? " PT1000-cell" : "", s->faults & KP_F_RTD_B ? " PT1000-air" : "",
                 s->faults & KP_F_INA ? " INA228" : "", s->faults & KP_F_STORAGE ? " mémoire" : "",
                 s->faults & KP_F_CAN ? " CAN" : "", s->faults & KP_F_OVERFLOW ? " débordement" : "",
                 s->faults & KP_F_LOG_DROP ? " pertes" : "");
        return b;
    }
    *c = KH_COL_GREEN;
    snprintf(b, len, "%s%s%s", s->state & KP_ST_LOGGING ? "enregistre" : "à l'arrêt",
             s->state & KP_ST_TIME_SYNC ? " · synchro" : " · sans synchro", s->state & KP_ST_WIFI ? " · Wi-Fi" : "");
    if (!(s->state & KP_ST_LOGGING)) *c = KH_COL_TEXT_MID;
    return b;
}

static void update_system(const kh_model_t *m, const kh_local_t *loc, int64_t now)
{
    char b[96], v[16];
    bool link = kh_link_ok(m, now);
    if (loc->demo) {
        snprintf(b, sizeof b, "Liaison : simulation locale");
        lv_obj_set_style_text_color(ui.sys.link, KH_COL_AMBER, 0);
    } else if (link) {
        snprintf(b, sizeof b, LV_SYMBOL_WIFI " Liaison OK  %d dBm", m->rssi);
        lv_obj_set_style_text_color(ui.sys.link, KH_COL_GREEN, 0);
    } else if (loc->binding) {
        snprintf(b, sizeof b, "Recherche du maître…");
        lv_obj_set_style_text_color(ui.sys.link, KH_COL_AMBER, 0);
    } else {
        snprintf(b, sizeof b, loc->bound ? "Liaison PERDUE" : "Non appairé");
        lv_obj_set_style_text_color(ui.sys.link, loc->bound ? KH_COL_RED : KH_COL_TEXT_MID, 0);
    }
    lv_label_set_text(ui.sys.link, b);
    fmt_dec(v, sizeof v, kh_loss_pct(m), 1);
    snprintf(b, sizeof b, "%lu paquets · pertes %s %%", (unsigned long)m->rx_pkts, v);
    lv_label_set_text(ui.sys.link2, b);

    if (kh_fresh(m->t_sync, now, KH_STALE_MS)) {
        bool utc = m->sync.flags & KP_SYNC_UTC, pps = m->sync.flags & KP_SYNC_PPS;
        if (utc) {
            uint64_t s = m->sync.us / 1000000ull;
            snprintf(b, sizeof b, "GPS %s  %02u:%02u:%02u UTC", pps ? "calé" : "sans PPS", (unsigned)(s / 3600 % 24),
                     (unsigned)(s / 60 % 60), (unsigned)(s % 60));
        } else {
            snprintf(b, sizeof b, "GPS : pas d'heure");
        }
        lv_obj_set_style_text_color(ui.sys.gps, utc ? (pps ? KH_COL_GREEN : KH_COL_AMBER) : KH_COL_AMBER, 0);
    } else {
        snprintf(b, sizeof b, "GPS : ---");
        lv_obj_set_style_text_color(ui.sys.gps, KH_COL_TEXT_DIM, 0);
    }
    lv_label_set_text(ui.sys.gps, b);

    for (int k = 0; k < 2; k++) {
        char t[80];
        lv_color_t c;
        node_text(&m->node[k], now, t, sizeof t, &c);
        snprintf(b, sizeof b, "Ch.%d : %s", k, t);
        lv_label_set_text(ui.sys.node[k], b);
        lv_obj_set_style_text_color(ui.sys.node[k], c, 0);
    }

    ui.bound = loc->bound;
    ui.demo = loc->demo;
    lv_label_set_text(ui.sys.lbl_bind, loc->bound ? LV_SYMBOL_CLOSE " DÉLIER" : LV_SYMBOL_WIFI " LIER");
    lv_obj_set_style_bg_color(ui.sys.btn_demo, loc->demo ? KH_COL_AMBER : KH_COL_CARD2, 0);
    lv_obj_set_style_text_color(ui.sys.lbl_demo, loc->demo ? KH_COL_BG : KH_COL_TEXT, 0);
    snprintf(b, sizeof b, LV_SYMBOL_IMAGE " %d %%", loc->brightness_pct);
    lv_label_set_text(ui.sys.lbl_lum, b);
    if (loc->bound)
        snprintf(b, sizeof b, "maître %02X:%02X:%02X:%02X:%02X:%02X", loc->master_mac[0], loc->master_mac[1],
                 loc->master_mac[2], loc->master_mac[3], loc->master_mac[4], loc->master_mac[5]);
    else
        b[0] = 0;
    lv_label_set_text(ui.sys.mac, b);
}

static void update_status(const kh_model_t *m, const kh_local_t *loc, int64_t now)
{
    if (loc->demo || (m->simulated && kh_link_ok(m, now))) {
        lv_label_set_text(ui.status, "SIMULATION");
        lv_obj_set_style_text_color(ui.status, KH_COL_AMBER, 0);
    } else if (kh_link_ok(m, now)) {
        lv_label_set_text(ui.status, "LIAISON OK");
        lv_obj_set_style_text_color(ui.status, KH_COL_GREEN, 0);
    } else {
        lv_label_set_text(ui.status, loc->bound ? LV_SYMBOL_WARNING " LIAISON PERDUE" : "NON APPAIRÉ");
        lv_obj_set_style_text_color(ui.status, loc->bound ? KH_COL_RED : KH_COL_TEXT_DIM, 0);
    }
}

static void update_alarm(const kh_model_t *m)
{
    if (!m->alarm_pending) {
        lv_obj_add_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char b[64], v1[16], v2[16];
    snprintf(b, sizeof b, "Chaussette %u", m->alarm_node);
    lv_label_set_text(ui.al.node, b);
    fmt_int(v1, sizeof v1, m->alarm_last.rms_mg);
    snprintf(b, sizeof b, "%s mg", v1);
    lv_label_set_text(ui.al.val, b);
    fmt_int(v1, sizeof v1, m->alarm_last.rpm[0]);
    fmt_int(v2, sizeof v2, m->alarm_last.rpm[1]);
    snprintf(b, sizeof b, "M0 %s · M1 %s tr/min", v1, v2);
    lv_label_set_text(ui.al.rpm, b);
    lv_obj_clear_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.al.root);
}

static void update_knob(const kh_knob_view_t *k)
{
    if (!k || !k->valid) {
        lv_obj_add_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN);
    /* LVGL : 0° à droite, sens horaire ; molette : 0° en haut */
    int a = (int)lroundf(k->angle_deg) + 270;
    a %= 360;
    if (a < 0) a += 360;
    lv_arc_set_angles(ui.knob_arc, (uint16_t)((a + 360 - 7) % 360), (uint16_t)((a + 7) % 360));
    lv_obj_set_style_arc_color(ui.knob_arc, k->tilt_mode ? KH_COL_AMBER : KH_COL_TEXT_MID, LV_PART_INDICATOR);
}

void kh_screen_update(const kh_model_t *m, const kh_local_t *loc, const kh_knob_view_t *knob, int64_t now)
{
    update_status(m, loc, now);
    update_motors(m, now);
    update_solar(m, now);
    update_vib(m, now);
    update_system(m, loc, now);
    update_alarm(m);
    update_knob(knob);
}
