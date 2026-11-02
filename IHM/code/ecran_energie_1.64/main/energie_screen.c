#include "energie_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* textes bornés volontairement : une troncature éventuelle est acceptable à l'écran */
#pragma GCC diagnostic ignored "-Wformat-truncation"

/* ------------------------------------------------------------------ géométrie (portrait 280 × 456) */
#define BAR_W 144          /* jauge de batterie, au centre */
#define BAR_H 356
#define BAR_TOP 46
#define BAR_CX 140
#define SOL_W 10           /* trait solaire, à gauche */
#define SOL_CX 36
#define ARR_W 60           /* zone de la flèche de bilan, à droite */
#define ARR_CX 246
#define ARR_LEN_MAX 160    /* longueur maximale de la flèche, du milieu vers le haut ou le bas */
#define ARR_SHAFT 14
#define ARR_HEAD_W 40
#define ARR_HEAD_H 26
#define NET_DEAD_W 25.0f   /* en dessous : bilan nul, pas de flèche */
#define WIFI_IDLE_MS 60000 /* fenêtre Wi-Fi refermée sans toucher l'écran */

static ke_cb_t s_cb;

static struct {
    lv_obj_t *scr, *tv, *tile[KE_PAGE_COUNT], *dots[KE_PAGE_COUNT], *status;
    struct {
        lv_obj_t *bar, *soc, *pct, *sol, *arrow;
        bool label_low; /* pourcentage en haut (charge < 50 %) */
    } en;
    struct {
        lv_obj_t *v[12];
    } det;
    struct {
        lv_obj_t *link, *link2, *btn_bind, *lbl_bind, *btn_demo, *rond, *lbl_demo, *btn_lum, *lbl_lum, *btn_wifi, *lbl_wifi;
        lv_obj_t *ota, *ver;
    } set;
    struct {
        lv_obj_t *root, *state, *info, *list, *btn_scan, *lbl_scan, *btn_forget, *btn_close;
        lv_obj_t *pw_root, *pw_title, *pw_ta, *pw_kb;
        uint32_t scan_gen;
        int ap_count;
        kh_wifi_ap_t ap[KH_WIFI_MAX_AP];
        char sel_ssid[33];
    } wf;
    bool demo;
    int lum;
} ui;

/* état de la flèche (dessinée à la main dans arrow_draw_cb) */
static struct {
    int len;      /* px, signé : + vers le haut (charge), − vers le bas (décharge) */
    bool valid;   /* bilan connu */
} s_arrow;

/* ------------------------------------------------------------------ format français */
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

/* « 3 h 20 », « 45 min », « > 99 h » */
static void fmt_hours(char *b, size_t n, float h)
{
    if (h >= 99.5f) { snprintf(b, n, "> 99 h"); return; }
    int min = (int)lroundf(h * 60.0f);
    if (min < 60) snprintf(b, n, "%d min", min);
    else snprintf(b, n, "%d h %02d", min / 60, min % 60);
}

/* « +350 W », « −1 808 W », « 2,4 kW » au-delà de 10 kW */
static void fmt_watts(char *b, size_t n, float w, bool sign)
{
    char v[24];
    fmt_int(v, sizeof v, labs(lroundf(w)));
    snprintf(b, n, "%s%s W", sign ? (w > 0.5f ? "+" : w < -0.5f ? "−" : "") : (w < -0.5f ? "−" : ""), v);
}

/* ------------------------------------------------------------------ écritures sans redessin inutile */
static bool set_text(lv_obj_t *l, const char *t)
{
    const char *cur = lv_label_get_text(l);
    if (cur && !strcmp(cur, t)) return false;
    lv_label_set_text(l, t);
    return true;
}
static void set_text_color(lv_obj_t *o, lv_color_t c)
{
    if (lv_obj_get_style_text_color(o, 0).full != c.full) lv_obj_set_style_text_color(o, c, 0);
}
static void set_bg_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel)
{
    if (lv_obj_get_style_bg_color(o, sel).full != c.full) lv_obj_set_style_bg_color(o, c, sel);
}
static void set_bar(lv_obj_t *b, int v)
{
    if (lv_bar_get_value(b) != v) lv_bar_set_value(b, v, LV_ANIM_OFF);
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

static lv_obj_t *vbar(lv_obj_t *par, int w, int h, int pad, int radius, lv_color_t c)
{
    lv_obj_t *b = lv_bar_create(par);
    lv_obj_set_size(b, w, h); /* plus haute que large : LVGL la remplit de bas en haut */
    lv_bar_set_range(b, 0, 1000);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, radius, LV_PART_MAIN);
    lv_obj_set_style_radius(b, radius > pad ? radius - pad : radius, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, KH_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, pad, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, c, LV_PART_INDICATOR);
    return b;
}

static void btn_cb(lv_event_t *e);

static lv_obj_t *button(lv_obj_t *par, const char *txt, int w, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_set_size(b, w, 48);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_color(b, KH_COL_CARD2, 0);
    lv_obj_set_style_bg_color(b, KH_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = label(b, &kh_font_18, KH_COL_TEXT, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(l, w - 24, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
    if (lbl_out) *lbl_out = l;
    return b;
}

static void tile_base(lv_obj_t *t)
{
    lv_obj_set_style_bg_color(t, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *title(lv_obj_t *t, const char *txt)
{
    lv_obj_t *l = label(t, &kh_font_18, KH_COL_TEXT_DIM, txt);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 30);
    return l;
}

/* ------------------------------------------------------------------ flèche de bilan */
static void arrow_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_coord_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    d.radius = 2;

    /* repère du zéro : le bilan nul est un trait horizontal (plus marqué quand il est mesuré) */
    const bool balanced = s_arrow.valid && s_arrow.len == 0;
    d.bg_color = balanced ? KH_COL_TEXT : KH_COL_CARD2;
    lv_area_t z = {cx - 22, cy - (balanced ? 3 : 2), cx + 22, cy + (balanced ? 3 : 1)};
    lv_draw_rect(ctx, &d, &z);
    if (!s_arrow.valid || s_arrow.len == 0) return;

    /* charge : vers le haut, en vert ; décharge : vers le bas, en blanc (consommer n'est pas une faute) */
    const bool up = s_arrow.len > 0;
    const int len = up ? s_arrow.len : -s_arrow.len;
    d.bg_color = up ? KH_COL_GREEN : KH_COL_TEXT;
    const lv_coord_t tip = up ? cy - len : cy + len;
    const lv_coord_t base = up ? tip + ARR_HEAD_H : tip - ARR_HEAD_H; /* base de la pointe */
    lv_area_t shaft = {cx - ARR_SHAFT / 2, up ? base - 1 : cy, cx + ARR_SHAFT / 2 - 1, up ? cy : base + 1};
    if ((up && base < cy) || (!up && base > cy)) lv_draw_rect(ctx, &d, &shaft);
    d.radius = 0;
    lv_point_t pts[3] = {{cx, tip}, {cx - ARR_HEAD_W / 2, base}, {cx + ARR_HEAD_W / 2, base}};
    lv_draw_polygon(ctx, &d, pts, 3);
}

static void arrow_set(bool valid, int len)
{
    if (valid == s_arrow.valid && len == s_arrow.len) return;
    s_arrow.valid = valid;
    s_arrow.len = len;
    lv_obj_invalidate(ui.en.arrow);
}

/* ------------------------------------------------------------------ page ÉNERGIE */
static void build_energy(lv_obj_t *t)
{
    tile_base(t);
    ui.en.sol = vbar(t, SOL_W, BAR_H, 0, SOL_W / 2, KH_COL_SUN);
    lv_obj_align(ui.en.sol, LV_ALIGN_TOP_MID, SOL_CX - KE_SCR_W / 2, BAR_TOP);

    ui.en.bar = vbar(t, BAR_W, BAR_H, 6, 22, KH_COL_GREEN);
    lv_obj_set_style_border_color(ui.en.bar, KH_COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.en.bar, 3, LV_PART_MAIN);
    lv_obj_align(ui.en.bar, LV_ALIGN_TOP_MID, BAR_CX - KE_SCR_W / 2, BAR_TOP);

    ui.en.soc = label(t, &kh_font_64, KH_COL_TEXT, "---");
    ui.en.pct = label(t, &kh_font_24, KH_COL_TEXT, "");

    ui.en.arrow = lv_obj_create(t);
    lv_obj_remove_style_all(ui.en.arrow);
    lv_obj_set_size(ui.en.arrow, ARR_W, BAR_H);
    lv_obj_clear_flag(ui.en.arrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui.en.arrow, LV_ALIGN_TOP_MID, ARR_CX - KE_SCR_W / 2, BAR_TOP);
    lv_obj_add_event_cb(ui.en.arrow, arrow_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* repères discrets sous les jauges */
    lv_obj_t *s = label(t, &kh_font_18, KH_COL_TEXT_DIM, LV_SYMBOL_CHARGE);
    lv_obj_align(s, LV_ALIGN_TOP_MID, SOL_CX - KE_SCR_W / 2, BAR_TOP + BAR_H + 6);
    s = label(t, &kh_font_18, KH_COL_TEXT_DIM, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(s, LV_ALIGN_TOP_MID, BAR_CX - KE_SCR_W / 2, BAR_TOP + BAR_H + 6);
}

/* Pourcentage dans la jauge, toujours loin du bord de remplissage : en bas (dans le plein) au-dessus
 * de 50 %, en haut (dans le vide) en dessous. Hystérésis de ±2 % pour qu'il ne saute pas. */
static void place_soc(bool low)
{
    if (low) lv_obj_align_to(ui.en.soc, ui.en.bar, LV_ALIGN_TOP_MID, -10, 22);
    else lv_obj_align_to(ui.en.soc, ui.en.bar, LV_ALIGN_BOTTOM_MID, -10, -22);
    lv_obj_align_to(ui.en.pct, ui.en.soc, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -10);
}

static void update_energy(const kh_energy_t *e, const ke_local_t *loc)
{
    char b[16];
    /* état de charge : vert, ambre sous 30 %, rouge sous 15 % (mêmes seuils que l'écran rond) */
    const lv_color_t sc = e->soc_pct < 15.0f ? KH_COL_RED : e->soc_pct < 30.0f ? KH_COL_AMBER : KH_COL_GREEN;
    set_bar(ui.en.bar, e->batt_ok ? (int)lroundf(e->soc_pct * 10.0f) : 0);
    set_bg_color(ui.en.bar, sc, LV_PART_INDICATOR);

    bool low = ui.en.label_low;
    if (!e->batt_ok) low = true;
    else if (e->soc_pct < 48.0f) low = true;
    else if (e->soc_pct > 52.0f) low = false;
    bool moved = low != ui.en.label_low;
    ui.en.label_low = low;

    if (e->batt_ok) snprintf(b, sizeof b, "%d", (int)lroundf(e->soc_pct));
    else snprintf(b, sizeof b, "---");
    bool changed = set_text(ui.en.soc, b);
    changed |= set_text(ui.en.pct, e->batt_ok ? "%" : "");
    /* dans le plein : texte noir sur la couleur ; dans le vide : blanc, ou la couleur d'alerte */
    lv_color_t tc = !e->batt_ok ? KH_COL_TEXT_DIM : !low ? KH_COL_BG : e->soc_pct < 30.0f ? sc : KH_COL_TEXT;
    set_text_color(ui.en.soc, tc);
    set_text_color(ui.en.pct, tc);
    if (moved || changed) place_soc(low);

    set_bar(ui.en.sol, e->solar_ok ? (int)lroundf(e->solar_pct * 10.0f) : 0);

    /* flèche : longueur en racine carrée du flux (les petits flux restent visibles), même échelle
     * dans les deux sens, pleine longueur à la puissance de référence des moteurs */
    int len = 0;
    if (e->net_ok && fabsf(e->net_w) >= NET_DEAD_W) {
        float full = loc->thrust_ref_w > 0 ? loc->thrust_ref_w : 3500.0f;
        float f = sqrtf(fminf(fabsf(e->net_w) / full, 1.0f));
        len = ARR_HEAD_H + (int)lroundf(f * (ARR_LEN_MAX - ARR_HEAD_H));
        len = (len / 4) * 4; /* pas de 4 px : pas de redessin pour un watt */
        if (e->net_w < 0) len = -len;
    }
    arrow_set(e->net_ok, len);
}

/* ------------------------------------------------------------------ page DÉTAILS */
enum { D_BAT_H, D_BAT1, D_BAT2, D_SOL_H, D_SOL1, D_SOL2, D_NET_H, D_NET1, D_MOT, D_AUT_H, D_AUT1, D_AUT2, D_COUNT };

static void build_details(lv_obj_t *t)
{
    tile_base(t);
    title(t, "DÉTAILS");
    static const struct { int y; uint8_t font; const char *txt; } L[D_COUNT] = {
        [D_BAT_H] = {64, 14, "BATTERIE"}, [D_BAT1] = {82, 24, "---"},  [D_BAT2] = {112, 18, ""},
        [D_SOL_H] = {148, 14, "SOLAIRE"}, [D_SOL1] = {166, 24, "---"}, [D_SOL2] = {196, 18, ""},
        [D_NET_H] = {232, 14, "BILAN · MOTEURS"}, [D_NET1] = {250, 24, "---"}, [D_MOT] = {280, 18, ""},
        [D_AUT_H] = {316, 14, "AUTONOMIE"}, [D_AUT1] = {334, 24, "---"}, [D_AUT2] = {364, 18, ""},
    };
    for (int i = 0; i < D_COUNT; i++) {
        const lv_font_t *f = L[i].font == 14 ? &kh_font_14 : L[i].font == 18 ? &kh_font_18 : &kh_font_24;
        lv_color_t c = L[i].font == 14 ? KH_COL_TEXT_DIM : L[i].font == 18 ? KH_COL_TEXT_MID : KH_COL_TEXT;
        ui.det.v[i] = label(t, f, c, L[i].txt);
        if (L[i].font == 14) lv_obj_set_style_text_letter_space(ui.det.v[i], 2, 0);
        lv_obj_align(ui.det.v[i], LV_ALIGN_TOP_MID, 0, L[i].y);
    }
}

static void update_details(const kh_model_t *m, const kh_energy_t *e, const ke_local_t *loc)
{
    char b[64], x[24], y[24];
    if (e->batt_ok) {
        fmt_dec(x, sizeof x, e->batt_v, 1);
        snprintf(b, sizeof b, "%d %%  ·  %s V", (int)lroundf(e->soc_pct), x);
        set_text(ui.det.v[D_BAT1], b);
        fmt_dec(x + 1, sizeof x - 1, fabsf(e->batt_a), 1);
        x[0] = e->batt_a >= 0.05f ? '+' : e->batt_a <= -0.05f ? '-' : ' ';
        if (x[0] == ' ') memmove(x, x + 1, strlen(x));
        fmt_int(y, sizeof y, lroundf(e->batt_wh));
        char z[16];
        fmt_dec(z, sizeof z, e->batt_ah, 1);
        snprintf(b, sizeof b, "%s A  ·  %s Wh  ·  %s Ah", x, y, z);
        set_text(ui.det.v[D_BAT2], b);
    } else {
        set_text(ui.det.v[D_BAT1], "---");
        set_text(ui.det.v[D_BAT2], "");
    }
    if (e->solar_ok) {
        fmt_watts(x, sizeof x, e->solar_w, false);
        snprintf(b, sizeof b, "%s  ·  %d %%", x, (int)lroundf(e->solar_pct));
        set_text(ui.det.v[D_SOL1], b);
        fmt_watts(x, sizeof x, m->solar.p_w[0], false);
        fmt_watts(y, sizeof y, m->solar.p_w[1], false);
        snprintf(b, sizeof b, "aile G %s  ·  aile D %s", x, y);
        set_text(ui.det.v[D_SOL2], b);
    } else {
        set_text(ui.det.v[D_SOL1], "---");
        set_text(ui.det.v[D_SOL2], "");
    }
    if (e->net_ok) {
        fmt_watts(x, sizeof x, e->net_w, true);
        snprintf(b, sizeof b, "%s  %s", x, e->net_w >= NET_DEAD_W ? "en charge" : e->net_w <= -NET_DEAD_W ? "en décharge" : "équilibre");
        set_text(ui.det.v[D_NET1], b);
        set_text_color(ui.det.v[D_NET1], e->net_w >= NET_DEAD_W ? KH_COL_GREEN : KH_COL_TEXT);
    } else {
        set_text(ui.det.v[D_NET1], "---");
        set_text_color(ui.det.v[D_NET1], KH_COL_TEXT);
    }
    if (!isnan(e->motor_w)) {
        fmt_watts(x, sizeof x, e->motor_w, false);
        float ref = loc->thrust_ref_w > 0 ? loc->thrust_ref_w : 3500.0f;
        snprintf(b, sizeof b, "moteurs %s  ·  %d %%", x, (int)lroundf(e->motor_w / ref * 100.0f));
    } else {
        snprintf(b, sizeof b, "moteurs ---");
    }
    set_text(ui.det.v[D_MOT], b);
    if (!e->batt_ok) {
        set_text(ui.det.v[D_AUT1], "---");
        set_text(ui.det.v[D_AUT2], "");
    } else {
        if (isinf(e->level_h)) snprintf(b, sizeof b, "palier illimité");
        else {
            fmt_hours(x, sizeof x, e->level_h);
            snprintf(b, sizeof b, "%s de palier", x);
        }
        set_text(ui.det.v[D_AUT1], b);
        set_text_color(ui.det.v[D_AUT1], e->level_h < 0.5f ? KH_COL_RED : KH_COL_TEXT);
        if (e->full_h == 0) snprintf(b, sizeof b, "batterie pleine");
        else if (isnan(e->full_h)) snprintf(b, sizeof b, "pas de recharge solaire");
        else {
            fmt_hours(x, sizeof x, e->full_h);
            snprintf(b, sizeof b, "pleine dans %s", x);
        }
        set_text(ui.det.v[D_AUT2], b);
    }
}

/* ------------------------------------------------------------------ fenêtre WI-FI */
static void style_dark_btn(lv_obj_t *b)
{
    lv_obj_set_style_bg_color(b, KH_COL_CARD, 0);
    lv_obj_set_style_bg_color(b, KH_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, KH_COL_TEXT, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
}

static lv_obj_t *overlay(void)
{
    lv_obj_t *r = lv_obj_create(lv_layer_top());
    lv_obj_set_size(r, KE_SCR_W, KE_SCR_H);
    lv_obj_center(r);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_bg_color(r, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    return r;
}

static bool wifi_open(void) { return !lv_obj_has_flag(ui.wf.root, LV_OBJ_FLAG_HIDDEN); }

static void pw_close(void)
{
    lv_obj_add_flag(ui.wf.pw_root, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui.wf.pw_ta, "");
}

static void wifi_close(void)
{
    pw_close();
    lv_obj_add_flag(ui.wf.root, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_show(void)
{
    lv_obj_clear_flag(ui.wf.root, LV_OBJ_FLAG_HIDDEN);
    lv_disp_trig_activity(NULL);
    if (s_cb.on_wifi_scan) s_cb.on_wifi_scan(); /* liste fraîche à chaque ouverture */
}

static void pw_open(const char *ssid, const char *text)
{
    char b[64];
    snprintf(b, sizeof b, "Mot de passe\n%s", ssid);
    lv_label_set_text(ui.wf.pw_title, b);
    lv_textarea_set_text(ui.wf.pw_ta, text ? text : "");
    lv_obj_clear_flag(ui.wf.pw_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.wf.pw_root);
}

static void kb_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY) {
        if (s_cb.on_wifi_connect) s_cb.on_wifi_connect(ui.wf.sel_ssid, lv_textarea_get_text(ui.wf.pw_ta));
        pw_close();
    } else if (c == LV_EVENT_CANCEL) {
        pw_close();
    }
}

static void ap_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= ui.wf.ap_count) return;
    strncpy(ui.wf.sel_ssid, ui.wf.ap[i].ssid, sizeof ui.wf.sel_ssid - 1);
    ui.wf.sel_ssid[sizeof ui.wf.sel_ssid - 1] = 0;
    if (!ui.wf.ap[i].secure) { /* réseau ouvert : connexion directe */
        if (s_cb.on_wifi_connect) s_cb.on_wifi_connect(ui.wf.sel_ssid, "");
        return;
    }
    pw_open(ui.wf.sel_ssid, "");
}

static void build_wifi(void)
{
    lv_obj_t *r = overlay();
    ui.wf.root = r;
    title(r, "WI-FI");
    ui.wf.state = label(r, &kh_font_18, KH_COL_TEXT_MID, "Non connecté");
    lv_label_set_long_mode(ui.wf.state, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(ui.wf.state, 256, 0);
    lv_obj_align(ui.wf.state, LV_ALIGN_TOP_MID, 0, 58);
    ui.wf.info = label(r, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_set_width(ui.wf.info, 256);
    lv_obj_set_style_text_align(ui.wf.info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.wf.info, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.wf.info, LV_ALIGN_TOP_MID, 0, 84);

    lv_obj_t *l = lv_list_create(r);
    lv_obj_set_size(l, 264, 210);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_row(l, 6, 0);
    ui.wf.list = l;

    ui.wf.btn_scan = button(r, "CHERCHER", 128, &ui.wf.lbl_scan);
    lv_obj_align(ui.wf.btn_scan, LV_ALIGN_TOP_MID, -68, 342);
    ui.wf.btn_forget = button(r, "OUBLIER", 128, NULL);
    lv_obj_align(ui.wf.btn_forget, LV_ALIGN_TOP_MID, 68, 342);
    ui.wf.btn_close = button(r, LV_SYMBOL_CLOSE " FERMER", 264, NULL);
    lv_obj_align(ui.wf.btn_close, LV_ALIGN_TOP_MID, 0, 398);

    /* saisie du mot de passe, au-dessus de la liste */
    lv_obj_t *p = overlay();
    ui.wf.pw_root = p;
    ui.wf.pw_title = label(p, &kh_font_18, KH_COL_TEXT_MID, "Mot de passe");
    lv_obj_set_style_text_align(ui.wf.pw_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.wf.pw_title, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *ta = lv_textarea_create(p);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, false); /* visible : on saisit sur un petit écran */
    lv_textarea_set_max_length(ta, 63);
    lv_obj_set_size(ta, 264, 46);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_text_font(ta, &kh_font_18, 0);
    lv_obj_set_style_bg_color(ta, KH_COL_CARD, 0);
    lv_obj_set_style_text_color(ta, KH_COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, KH_COL_ACCENT, 0);
    ui.wf.pw_ta = ta;
    lv_obj_t *hint = label(p, &kh_font_14, KH_COL_TEXT_DIM, LV_SYMBOL_OK " connecter   " LV_SYMBOL_KEYBOARD " annuler");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_t *kb = lv_keyboard_create(p);
    lv_obj_set_size(kb, KE_SCR_W, 250);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_text_font(kb, &kh_font_18, 0);
    lv_obj_set_style_bg_color(kb, KH_COL_BG, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 3, 0);
    lv_obj_set_style_bg_color(kb, KH_COL_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, KH_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, KH_COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KH_COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x3A3F4E), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_ALL, NULL);
    ui.wf.pw_kb = kb;
}

static void rebuild_ap_list(const kh_wifi_status_t *w)
{
    lv_obj_clean(ui.wf.list);
    ui.wf.ap_count = w->ap_count;
    memcpy(ui.wf.ap, w->ap, sizeof ui.wf.ap);
    if (!w->ap_count) {
        char m[64];
        if (!w->scan_gen) m[0] = 0;
        else if (w->scan_err) snprintf(m, sizeof m, "Balayage impossible (0x%x)", w->scan_err);
        else snprintf(m, sizeof m, "Aucun réseau trouvé");
        label(ui.wf.list, &kh_font_14, w->scan_err ? KH_COL_AMBER : KH_COL_TEXT_DIM, m);
        return;
    }
    for (int i = 0; i < w->ap_count; i++) {
        char b[64];
        int bars = w->ap[i].rssi > -60 ? 3 : w->ap[i].rssi > -72 ? 2 : 1;
        snprintf(b, sizeof b, "%s%s", w->ap[i].secure ? "" : "(ouvert) ", w->ap[i].ssid);
        lv_obj_t *btn = lv_btn_create(ui.wf.list);
        lv_obj_set_size(btn, lv_pct(100), 44);
        style_dark_btn(btn);
        lv_obj_t *l = label(btn, &kh_font_18, KH_COL_TEXT, b);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 180);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 2, 0);
        char r[8];
        snprintf(r, sizeof r, "%d", w->ap[i].rssi);
        lv_obj_t *rl = label(btn, &kh_font_14, bars == 3 ? KH_COL_GREEN : bars == 2 ? KH_COL_AMBER : KH_COL_RED, r);
        lv_obj_align(rl, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_add_event_cb(btn, ap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void update_wifi(const ke_local_t *loc)
{
    const kh_wifi_status_t *w = &loc->wifi;
    char b[96];
    switch (w->state) {
    case KH_WIFI_CONNECTED:
        snprintf(b, sizeof b, LV_SYMBOL_WIFI " %s", w->ssid);
        set_text_color(ui.wf.state, KH_COL_GREEN);
        break;
    case KH_WIFI_CONNECTING:
        snprintf(b, sizeof b, "Connexion à %s…", w->ssid);
        set_text_color(ui.wf.state, KH_COL_AMBER);
        break;
    case KH_WIFI_FAILED:
        snprintf(b, sizeof b, "Échec : %s", w->ssid);
        set_text_color(ui.wf.state, KH_COL_RED);
        break;
    default:
        snprintf(b, sizeof b, "Non connecté");
        set_text_color(ui.wf.state, KH_COL_TEXT_MID);
    }
    set_text(ui.wf.state, b);
    const bool off_chan = w->state == KH_WIFI_CONNECTED && loc->espnow_channel && w->channel != loc->espnow_channel;
    if (w->state == KH_WIFI_CONNECTED) {
        if (off_chan)
            snprintf(b, sizeof b, "%s · canal %u\n" LV_SYMBOL_WARNING " liaison avion (canal %u) coupée", w->ip, w->channel,
                     loc->espnow_channel);
        else snprintf(b, sizeof b, "%s · canal %u · %d dBm", w->ip, w->channel, w->rssi);
    } else {
        snprintf(b, sizeof b, w->scanning ? "Recherche des réseaux…" : "Touchez un réseau pour vous connecter");
    }
    set_text(ui.wf.info, b);
    set_text_color(ui.wf.info, off_chan ? KH_COL_AMBER : KH_COL_TEXT_DIM);
    set_text(ui.wf.lbl_scan, w->scanning ? "…" : "CHERCHER");
    if (w->scan_gen != ui.wf.scan_gen) {
        ui.wf.scan_gen = w->scan_gen;
        rebuild_ap_list(w);
    }
}

/* ------------------------------------------------------------------ page RÉGLAGES */
static void build_settings(lv_obj_t *t)
{
    tile_base(t);
    title(t, "RÉGLAGES");
    ui.set.link = label(t, &kh_font_18, KH_COL_TEXT, "");
    lv_obj_align(ui.set.link, LV_ALIGN_TOP_MID, 0, 62);
    ui.set.link2 = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.set.link2, LV_ALIGN_TOP_MID, 0, 88);
    ui.set.btn_bind = button(t, LV_SYMBOL_REFRESH " RÉAPPAIRER", 236, &ui.set.lbl_bind);
    lv_obj_align(ui.set.btn_bind, LV_ALIGN_TOP_MID, 0, 116);
    ui.set.btn_demo = button(t, "SIMULATION", 236, &ui.set.lbl_demo);
    lv_obj_align(ui.set.btn_demo, LV_ALIGN_TOP_MID, 0, 174);
    ui.set.btn_lum = button(t, LV_SYMBOL_IMAGE " 100 %", 236, &ui.set.lbl_lum);
    lv_obj_align(ui.set.btn_lum, LV_ALIGN_TOP_MID, 0, 232);
    ui.set.btn_wifi = button(t, LV_SYMBOL_WIFI " WI-FI", 236, &ui.set.lbl_wifi);
    lv_obj_align(ui.set.btn_wifi, LV_ALIGN_TOP_MID, 0, 290);
    ui.set.ota = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_add_flag(ui.set.ota, LV_OBJ_FLAG_CLICKABLE); /* toucher : vérifier tout de suite */
    lv_obj_set_ext_click_area(ui.set.ota, 12);
    lv_obj_add_event_cb(ui.set.ota, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(ui.set.ota, LV_ALIGN_TOP_MID, 0, 346);
    ui.set.rond = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.set.rond, LV_ALIGN_TOP_MID, 0, 366);
    ui.set.ver = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.set.ver, LV_ALIGN_TOP_MID, 0, 386);
}

static void update_settings(const kh_model_t *m, const ke_local_t *loc, int64_t now)
{
    char b[96];
    ui.demo = loc->demo;
    const kh_peer_status_t *p = &loc->peer;
    if (p->alive) {
        snprintf(b, sizeof b, LV_SYMBOL_OK " écran rond v%s", p->hello.fw);
        set_text_color(ui.set.link, KH_COL_TEXT);
    } else if (p->paired) {
        snprintf(b, sizeof b, LV_SYMBOL_WARNING " écran rond perdu");
        set_text_color(ui.set.link, KH_COL_RED);
    } else {
        snprintf(b, sizeof b, "Recherche de l'écran rond…");
        set_text_color(ui.set.link, KH_COL_AMBER);
    }
    set_text(ui.set.link, b);
    if (p->paired)
        snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X · %d dBm · canal %u%s", p->mac[0], p->mac[1], p->mac[2], p->mac[3],
                 p->mac[4], p->mac[5], p->rssi, p->channel, kh_link_ok(m, now) && m->simulated ? " · simulé" : "");
    else
        b[0] = 0;
    set_text(ui.set.link2, b);
    set_text(ui.set.lbl_demo, loc->demo ? "SIMULATION : OUI" : "SIMULATION : NON");
    set_bg_color(ui.set.btn_demo, loc->demo ? KH_COL_AMBER : KH_COL_CARD2, 0);
    set_text_color(ui.set.lbl_demo, loc->demo ? KH_COL_BG : KH_COL_TEXT);
    ui.lum = loc->brightness_pct;
    snprintf(b, sizeof b, LV_SYMBOL_IMAGE " LUMINOSITÉ %d %%", loc->brightness_pct);
    set_text(ui.set.lbl_lum, b);
    if (loc->wifi.state == KH_WIFI_CONNECTED) snprintf(b, sizeof b, LV_SYMBOL_WIFI " %s", loc->wifi.ssid);
    else snprintf(b, sizeof b, LV_SYMBOL_WIFI " WI-FI : non connecté");
    set_text(ui.set.lbl_wifi, b);

    const kh_ota_status_t *o = &loc->ota;
    char st[48];
    switch (o->state) {
    case KH_OTA_UP_TO_DATE: snprintf(st, sizeof st, "à jour"); break;
    case KH_OTA_DOWNLOADING: snprintf(st, sizeof st, "mise à jour v%s %d %%", o->remote, o->progress_pct); break;
    case KH_OTA_DEFERRED: snprintf(st, sizeof st, "v%s après arrêt moteurs", o->remote); break;
    case KH_OTA_FAILED: snprintf(st, sizeof st, "échec mise à jour v%s", o->remote); break;
    case KH_OTA_CHECK_FAIL: snprintf(st, sizeof st, "GitHub injoignable"); break;
    default: snprintf(st, sizeof st, "MAJ auto avec le Wi-Fi"); break;
    }
    snprintf(b, sizeof b, "écran énergie v%s · %s", o->running, st);
    set_text(ui.set.ota, b);
    set_text_color(ui.set.ota, o->state == KH_OTA_DOWNLOADING ? KH_COL_AMBER : o->state == KH_OTA_FAILED ? KH_COL_RED : KH_COL_TEXT_DIM);
    snprintf(b, sizeof b, "carte %s", loc->board);
    set_text(ui.set.ver, b);

    /* mise à jour de l'écran rond, poussée par cet écran */
    const kh_ota_peer_status_t *rd = &loc->rond;
    switch (rd->state) {
    case KHP_UPD_UP_TO_DATE: snprintf(b, sizeof b, "écran rond v%s · à jour", rd->remote); break;
    case KHP_UPD_DOWNLOADING: snprintf(b, sizeof b, "écran rond : téléchargement v%s %u %%", rd->remote, rd->pct); break;
    case KHP_UPD_WAITING: snprintf(b, sizeof b, "écran rond : v%s prête (moteurs à l'arrêt)", rd->remote); break;
    case KHP_UPD_SENDING: snprintf(b, sizeof b, "écran rond : envoi v%s %u %%", rd->remote, rd->pct); break;
    case KHP_UPD_FAILED: snprintf(b, sizeof b, "écran rond : échec mise à jour v%s", rd->remote); break;
    default: snprintf(b, sizeof b, p->alive ? "écran rond : MAJ via ce Wi-Fi" : ""); break;
    }
    set_text(ui.set.rond, b);
    set_text_color(ui.set.rond, rd->state == KHP_UPD_SENDING || rd->state == KHP_UPD_DOWNLOADING ? KH_COL_AMBER
                                : rd->state == KHP_UPD_FAILED                                    ? KH_COL_RED
                                                                                                 : KH_COL_TEXT_DIM);
}

/* ------------------------------------------------------------------ boutons */
static void btn_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    if (b == ui.set.btn_bind && s_cb.on_repair) s_cb.on_repair();
    else if (b == ui.set.btn_demo && s_cb.on_demo) s_cb.on_demo(!ui.demo);
    else if (b == ui.set.btn_lum) {
        ui.lum = ui.lum >= 100 ? 60 : ui.lum >= 60 ? 30 : 100;
        if (s_cb.on_brightness) s_cb.on_brightness(ui.lum);
    } else if (b == ui.set.btn_wifi) wifi_show();
    else if (b == ui.set.ota && s_cb.on_ota_check) s_cb.on_ota_check();
    else if (b == ui.wf.btn_scan && s_cb.on_wifi_scan) s_cb.on_wifi_scan();
    else if (b == ui.wf.btn_forget && s_cb.on_wifi_forget) s_cb.on_wifi_forget();
    else if (b == ui.wf.btn_close) wifi_close();
}

/* ------------------------------------------------------------------ bandeau et pastilles */
static void update_status(const kh_model_t *m, const ke_local_t *loc, int64_t now)
{
    /* on ne dit rien quand tout va bien */
    if (loc->demo || (m->simulated && kh_link_ok(m, now))) {
        set_text(ui.status, "SIMULATION");
        set_text_color(ui.status, KH_COL_AMBER);
    } else if (kh_link_ok(m, now)) {
        set_text(ui.status, "");
    } else if (!loc->peer.alive) {
        set_text(ui.status, loc->peer.paired ? LV_SYMBOL_WARNING " ÉCRAN ROND PERDU" : "RECHERCHE ÉCRAN ROND");
        set_text_color(ui.status, loc->peer.paired ? KH_COL_RED : KH_COL_AMBER);
    } else {
        set_text(ui.status, LV_SYMBOL_WARNING " PAS DE DONNÉES");
        set_text_color(ui.status, KH_COL_RED);
    }
}

static void tv_cb(lv_event_t *e)
{
    (void)e;
    ke_page_t p = ke_screen_page();
    for (int i = 0; i < KE_PAGE_COUNT; i++) {
        lv_obj_set_size(ui.dots[i], i == (int)p ? 18 : 7, 7);
        lv_obj_set_style_bg_color(ui.dots[i], i == (int)p ? KH_COL_TEXT : KH_COL_CARD2, 0);
    }
    lv_disp_trig_activity(NULL);
}

/* ------------------------------------------------------------------ API */
void ke_screen_init(const ke_cb_t *cb)
{
    memset(&ui, 0, sizeof ui);
    if (cb) s_cb = *cb;
    ui.lum = 100;

    ui.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui.scr, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(ui.scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(ui.scr, &kh_font_18, 0);

    /* pages côte à côte : RÉGLAGES · DÉTAILS · ÉNERGIE ; on démarre sur ÉNERGIE, et glisser le
     * doigt vers la droite fait apparaître DÉTAILS puis RÉGLAGES */
    ui.tv = lv_tileview_create(ui.scr);
    lv_obj_set_size(ui.tv, KE_SCR_W, KE_SCR_H);
    lv_obj_set_style_bg_color(ui.tv, KH_COL_BG, 0);
    lv_obj_set_scrollbar_mode(ui.tv, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < KE_PAGE_COUNT; i++) {
        lv_dir_t dir = (i > 0 ? LV_DIR_LEFT : 0) | (i < KE_PAGE_COUNT - 1 ? LV_DIR_RIGHT : 0);
        ui.tile[i] = lv_tileview_add_tile(ui.tv, (uint8_t)i, 0, dir);
    }
    build_settings(ui.tile[KE_PAGE_SETTINGS]);
    build_details(ui.tile[KE_PAGE_DETAILS]);
    build_energy(ui.tile[KE_PAGE_ENERGY]);

    ui.status = label(ui.scr, &kh_font_18, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.status, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *row = lv_obj_create(ui.scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 80, 10);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -10);
    for (int i = 0; i < KE_PAGE_COUNT; i++) {
        ui.dots[i] = lv_obj_create(row);
        lv_obj_remove_style_all(ui.dots[i]);
        lv_obj_set_style_radius(ui.dots[i], 4, 0);
        lv_obj_set_style_bg_opa(ui.dots[i], LV_OPA_COVER, 0);
    }

    build_wifi();
    lv_obj_add_event_cb(ui.tv, tv_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_tile_id(ui.tv, KE_PAGE_ENERGY, 0, LV_ANIM_OFF);
    tv_cb(NULL);
    lv_scr_load(ui.scr);
}

ke_page_t ke_screen_page(void)
{
    lv_obj_t *act = lv_tileview_get_tile_act(ui.tv);
    for (int i = 0; i < KE_PAGE_COUNT; i++)
        if (act == ui.tile[i]) return (ke_page_t)i;
    return KE_PAGE_ENERGY;
}

void ke_screen_show_page(ke_page_t p, bool anim)
{
    if (p >= KE_PAGE_COUNT) return;
    lv_obj_set_tile_id(ui.tv, (uint32_t)p, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    tv_cb(NULL);
}

void ke_screen_update(const kh_model_t *m, const ke_local_t *loc, int64_t now)
{
    kh_energy_cfg_t cfg = {.batt_wh = loc->batt_wh, .pv_peak_w = loc->pv_peak_w, .level_w = loc->level_w};
    kh_energy_t e;
    kh_energy(m, now, &cfg, &e);

    update_status(m, loc, now);
    switch (ke_screen_page()) {
    case KE_PAGE_ENERGY: update_energy(&e, loc); break;
    case KE_PAGE_DETAILS: update_details(m, &e, loc); break;
    case KE_PAGE_SETTINGS: update_settings(m, loc, now); break;
    default: break;
    }
    if (wifi_open()) update_wifi(loc);

    /* retour automatique à la page énergie : 10 s sans toucher (60 s dans la fenêtre Wi-Fi) */
    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (wifi_open()) {
        if (idle > WIFI_IDLE_MS) {
            wifi_close();
            ke_screen_show_page(KE_PAGE_ENERGY, false);
        }
    } else if (ke_screen_page() != KE_PAGE_ENERGY && idle > KE_RETURN_MS) {
        ke_screen_show_page(KE_PAGE_ENERGY, true);
        update_energy(&e, loc);
    }
}

void ke_screen_debug_wifi(bool open)
{
    if (open) lv_obj_clear_flag(ui.wf.root, LV_OBJ_FLAG_HIDDEN);
    else wifi_close();
}

void ke_screen_debug_password(const char *ssid, const char *text)
{
    if (!ssid) pw_close();
    else pw_open(ssid, text);
}
