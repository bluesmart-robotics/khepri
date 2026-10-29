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

#define G_MAX 1200
#define VIB_MAX 8000

static const char *PAGE_TITLE[KH_PAGE_COUNT] = {"MOTEURS", "SOLAIRE", "VIBRATIONS", "SYSTÈME", "WI-FI"};

static kh_screen_cb_t s_cb;

static struct {
    lv_obj_t *scr, *tv, *tile[KH_PAGE_COUNT];
    lv_obj_t *dots[KH_PAGE_COUNT];
    lv_obj_t *status;   /* liaison / simulation, en haut */
    lv_obj_t *knob_arc; /* indicateur de molette sur le pourtour */

    struct {
        lv_obj_t *ring, *over, *pct, *kw, *axes, *vib[2];
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
    struct {
        lv_obj_t *state, *info, *list, *btn_scan, *lbl_scan, *btn_forget, *ota, *ota2;
        lv_obj_t *pw_root, *pw_title, *pw_ta, *pw_kb;
        uint32_t scan_gen;
        bool first_scan_done;
        char sel_ssid[33];
        kh_wifi_ap_t ap[KH_WIFI_MAX_AP];
        int ap_count;
    } wifi;

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

/* ------------------------------------------------------------------ écritures sans redessin inutile
 * Réécrire un texte ou une couleur identique force LVGL à redessiner la zone ; à 10 Hz sur toute
 * l'interface, la tâche d'affichage ne s'arrêtait plus. On ne touche qu'à ce qui change. */
static bool set_text(lv_obj_t *l, const char *t)
{
    const char *cur = lv_label_get_text(l);
    if (cur && !strcmp(cur, t)) return false;
    lv_label_set_text(l, t);
    return true;
}
static void set_text_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel)
{
    if (lv_obj_get_style_text_color(o, sel).full != c.full) lv_obj_set_style_text_color(o, c, sel);
}
static void set_bg_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel)
{
    if (lv_obj_get_style_bg_color(o, sel).full != c.full) lv_obj_set_style_bg_color(o, c, sel);
}
static void set_arc_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t sel)
{
    if (lv_obj_get_style_arc_color(o, sel).full != c.full) lv_obj_set_style_arc_color(o, c, sel);
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);
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
/* Jauge unique de poussée : anneau vert sur tout le pourtour, départ en bas, sens horaire.
 * 100 % = puissance de référence (3,5 kW par défaut). Au-delà, un second tour ambre recouvre le
 * premier : 150 % = un tour vert complet + un demi-tour ambre. */
static lv_obj_t *ring_arc(lv_obj_t *par, lv_color_t c, bool with_track)
{
    lv_obj_t *a = lv_arc_create(par);
    lv_obj_set_size(a, 446, 446);
    lv_obj_center(a);
    lv_arc_set_rotation(a, 90); /* 0 = en bas */
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, KH_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, with_track ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

static void build_motors(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_MOTORS);
    ui.mot.ring = ring_arc(t, KH_COL_GREEN, true);
    ui.mot.over = ring_arc(t, KH_COL_AMBER, false);

    lv_obj_t *cap = label(t, &kh_font_14, KH_COL_TEXT_DIM, "POUSSÉE");
    lv_obj_set_style_text_letter_space(cap, 3, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, -86);
    ui.mot.pct = label(t, &kh_font_64, KH_COL_TEXT, "---");
    lv_obj_align(ui.mot.pct, LV_ALIGN_CENTER, 0, -34);
    ui.mot.kw = label(t, &kh_font_18, KH_COL_TEXT_MID, "");
    lv_obj_align(ui.mot.kw, LV_ALIGN_CENTER, 0, 22);
    ui.mot.axes = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.mot.axes, LV_ALIGN_CENTER, 0, 50);
    for (int k = 0; k < 2; k++) {
        ui.mot.vib[k] = label(t, &kh_font_18, KH_COL_TEXT_DIM, "");
        lv_obj_align(ui.mot.vib[k], LV_ALIGN_CENTER, k == 0 ? -66 : 66, 122);
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
    lv_obj_set_style_text_align(ui.sys.mac, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.sys.mac, LV_ALIGN_CENTER, 0, 166);
}


/* ------------------------------------------------------------------ page WI-FI */
static void style_dark_btn(lv_obj_t *b)
{
    lv_obj_set_style_bg_color(b, KH_COL_CARD, 0);
    lv_obj_set_style_bg_color(b, KH_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, KH_COL_TEXT, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
}

static void pw_close(void)
{
    lv_obj_add_flag(ui.wifi.pw_root, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui.wifi.pw_ta, "");
}

static void kb_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY) {
        if (s_cb.on_wifi_connect) s_cb.on_wifi_connect(ui.wifi.sel_ssid, lv_textarea_get_text(ui.wifi.pw_ta));
        pw_close();
        kh_screen_show_page(KH_PAGE_WIFI, false);
    } else if (c == LV_EVENT_CANCEL) {
        pw_close();
    }
}

static void ap_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= ui.wifi.ap_count) return;
    strncpy(ui.wifi.sel_ssid, ui.wifi.ap[i].ssid, sizeof ui.wifi.sel_ssid - 1);
    ui.wifi.sel_ssid[sizeof ui.wifi.sel_ssid - 1] = 0;
    if (!ui.wifi.ap[i].secure) { /* réseau ouvert : connexion directe */
        if (s_cb.on_wifi_connect) s_cb.on_wifi_connect(ui.wifi.sel_ssid, "");
        return;
    }
    char b[64];
    snprintf(b, sizeof b, "Mot de passe\n%s", ui.wifi.sel_ssid);
    lv_label_set_text(ui.wifi.pw_title, b);
    lv_textarea_set_text(ui.wifi.pw_ta, "");
    lv_obj_clear_flag(ui.wifi.pw_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.wifi.pw_root);
}

/* Aperçus PC : ouvre (ssid non nul) ou ferme la saisie du mot de passe */
void kh_screen_debug_open_password(const char *ssid, const char *text)
{
    if (!ssid) {
        pw_close();
        return;
    }
    char b[64];
    snprintf(b, sizeof b, "Mot de passe\n%s", ssid);
    lv_label_set_text(ui.wifi.pw_title, b);
    lv_textarea_set_text(ui.wifi.pw_ta, text ? text : "");
    lv_obj_clear_flag(ui.wifi.pw_root, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_btn_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    if (b == ui.wifi.btn_scan && s_cb.on_wifi_scan) s_cb.on_wifi_scan();
    else if (b == ui.wifi.btn_forget && s_cb.on_wifi_forget) s_cb.on_wifi_forget();
    else if (b == ui.wifi.ota && s_cb.on_ota_check) s_cb.on_ota_check();
}

static void build_wifi(lv_obj_t *t)
{
    tile_base(t, KH_PAGE_WIFI);
    ui.wifi.state = label(t, &kh_font_18, KH_COL_TEXT_MID, "Non connecté");
    lv_obj_align(ui.wifi.state, LV_ALIGN_CENTER, 0, -134);
    ui.wifi.info = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_set_width(ui.wifi.info, 330);
    lv_obj_set_style_text_align(ui.wifi.info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.wifi.info, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.wifi.info, LV_ALIGN_CENTER, 0, -100);

    lv_obj_t *l = lv_list_create(t);
    lv_obj_set_size(l, 330, 168);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_row(l, 6, 0);
    ui.wifi.list = l;

    ui.wifi.btn_scan = button(t, LV_SYMBOL_REFRESH " CHERCHER", 160, &ui.wifi.lbl_scan);
    lv_obj_align(ui.wifi.btn_scan, LV_ALIGN_CENTER, -84, 116);
    lv_obj_remove_event_cb(ui.wifi.btn_scan, btn_cb);
    lv_obj_add_event_cb(ui.wifi.btn_scan, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    ui.wifi.btn_forget = button(t, LV_SYMBOL_TRASH " OUBLIER", 150, NULL);
    lv_obj_align(ui.wifi.btn_forget, LV_ALIGN_CENTER, 86, 116);
    lv_obj_remove_event_cb(ui.wifi.btn_forget, btn_cb);
    lv_obj_add_event_cb(ui.wifi.btn_forget, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ligne mise à jour : touchable pour forcer une vérification */
    ui.wifi.ota = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_add_flag(ui.wifi.ota, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ui.wifi.ota, 12);
    lv_obj_add_event_cb(ui.wifi.ota, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(ui.wifi.ota, LV_ALIGN_CENTER, 0, 160);
    ui.wifi.ota2 = label(t, &kh_font_14, KH_COL_TEXT_DIM, "");
    lv_obj_align(ui.wifi.ota2, LV_ALIGN_CENTER, 0, 180);

    /* saisie du mot de passe : plein écran au-dessus des pages */
    lv_obj_t *r = lv_obj_create(lv_layer_top());
    lv_obj_set_size(r, KH_SCR_W, KH_SCR_H);
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r, KH_COL_BG, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui.wifi.pw_root = r;
    ui.wifi.pw_title = label(r, &kh_font_18, KH_COL_TEXT_MID, "Mot de passe");
    lv_obj_set_style_text_align(ui.wifi.pw_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui.wifi.pw_title, LV_ALIGN_CENTER, 0, -158);
    lv_obj_t *ta = lv_textarea_create(r);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, false); /* visible : on saisit sur un petit écran */
    lv_textarea_set_max_length(ta, 63);
    lv_obj_set_size(ta, 320, 48);
    lv_obj_align(ta, LV_ALIGN_CENTER, 0, -104);
    lv_obj_set_style_text_font(ta, &kh_font_18, 0);
    lv_obj_set_style_bg_color(ta, KH_COL_CARD, 0);
    lv_obj_set_style_text_color(ta, KH_COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, KH_COL_ACCENT, 0);
    ui.wifi.pw_ta = ta;
    lv_obj_t *kb = lv_keyboard_create(r);
    lv_obj_set_size(kb, 380, 200);
    lv_obj_align(kb, LV_ALIGN_CENTER, 0, 48);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_text_font(kb, &kh_font_18, 0);
    lv_obj_set_style_bg_color(kb, KH_COL_BG, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 4, 0);
    lv_obj_set_style_bg_color(kb, KH_COL_CARD2, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(kb, KH_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, KH_COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KH_COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x3A3F4E), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_ALL, NULL);
    ui.wifi.pw_kb = kb;
    lv_obj_t *hint = label(r, &kh_font_14, KH_COL_TEXT_DIM, LV_SYMBOL_OK " connecter   " LV_SYMBOL_KEYBOARD " annuler");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 172);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
}

static void rebuild_ap_list(const kh_wifi_status_t *w)
{
    lv_obj_clean(ui.wifi.list);
    ui.wifi.ap_count = w->ap_count;
    memcpy(ui.wifi.ap, w->ap, sizeof ui.wifi.ap);
    if (!w->ap_count) {
        char m[64];
        if (!w->scan_gen) m[0] = 0;
        else if (w->scan_err) snprintf(m, sizeof m, "Balayage impossible (erreur 0x%x)", w->scan_err);
        else snprintf(m, sizeof m, "Aucun réseau trouvé");
        label(ui.wifi.list, &kh_font_14, w->scan_err ? KH_COL_AMBER : KH_COL_TEXT_DIM, m);
        return;
    }
    for (int i = 0; i < w->ap_count; i++) {
        char b[64];
        int bars = w->ap[i].rssi > -60 ? 3 : w->ap[i].rssi > -72 ? 2 : 1;
        snprintf(b, sizeof b, "%s%s", w->ap[i].secure ? "" : "(ouvert) ", w->ap[i].ssid);
        lv_obj_t *btn = lv_btn_create(ui.wifi.list);
        lv_obj_set_size(btn, lv_pct(100), 44);
        style_dark_btn(btn);
        lv_obj_t *l = label(btn, &kh_font_18, KH_COL_TEXT, b);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 230);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
        char r[16];
        snprintf(r, sizeof r, "%s %d", bars == 3 ? LV_SYMBOL_WIFI : "", w->ap[i].rssi);
        lv_obj_t *rl = label(btn, &kh_font_14, bars == 3 ? KH_COL_GREEN : bars == 2 ? KH_COL_AMBER : KH_COL_RED, r);
        lv_obj_align(rl, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_add_event_cb(btn, ap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void update_wifi(const kh_local_t *loc, int64_t now)
{
    const kh_wifi_status_t *w = &loc->wifi;
    char b[96];
    switch (w->state) {
    case KH_WIFI_CONNECTED:
        snprintf(b, sizeof b, LV_SYMBOL_WIFI " %s", w->ssid);
        set_text_color(ui.wifi.state, KH_COL_GREEN, 0);
        break;
    case KH_WIFI_CONNECTING:
        snprintf(b, sizeof b, "Connexion à %s…", w->ssid);
        set_text_color(ui.wifi.state, KH_COL_AMBER, 0);
        break;
    case KH_WIFI_FAILED:
        snprintf(b, sizeof b, "Échec : %s", w->ssid);
        set_text_color(ui.wifi.state, KH_COL_RED, 0);
        break;
    default:
        snprintf(b, sizeof b, "Non connecté");
        set_text_color(ui.wifi.state, KH_COL_TEXT_MID, 0);
    }
    set_text(ui.wifi.state, b);

    if (w->state == KH_WIFI_CONNECTED) {
        if (loc->espnow_channel && w->channel != loc->espnow_channel)
            snprintf(b, sizeof b, "%s · canal %u · %d dBm\n" LV_SYMBOL_WARNING " liaison avion sur canal %u : coupée", w->ip,
                     w->channel, w->rssi, loc->espnow_channel);
        else
            snprintf(b, sizeof b, "%s · canal %u · %d dBm", w->ip, w->channel, w->rssi);
    } else {
        snprintf(b, sizeof b, w->scanning ? "Recherche des réseaux…" : "Touchez un réseau pour vous connecter");
    }
    set_text(ui.wifi.info, b);
    set_text_color(ui.wifi.info,
                                w->state == KH_WIFI_CONNECTED && loc->espnow_channel && w->channel != loc->espnow_channel
                                    ? KH_COL_AMBER
                                    : KH_COL_TEXT_DIM,
                                0);
    set_text(ui.wifi.lbl_scan, w->scanning ? LV_SYMBOL_REFRESH " …" : LV_SYMBOL_REFRESH " CHERCHER");

    if (w->scan_gen != ui.wifi.scan_gen) {
        ui.wifi.scan_gen = w->scan_gen;
        rebuild_ap_list(w);
    }
    /* premier balayage automatique quand on arrive sur la page */
    if (!ui.wifi.first_scan_done && kh_screen_page() == KH_PAGE_WIFI && s_cb.on_wifi_scan) {
        ui.wifi.first_scan_done = true;
        s_cb.on_wifi_scan();
    }

    const kh_ota_status_t *o = &loc->ota;
    long ago = o->last_check_ms ? (long)((now - o->last_check_ms) / 1000) : -1;
    switch (o->state) {
    case KH_OTA_UP_TO_DATE: snprintf(b, sizeof b, "v%s · à jour", o->running); break;
    case KH_OTA_DOWNLOADING: snprintf(b, sizeof b, "Mise à jour v%s : %d %%", o->remote, o->progress_pct); break;
    case KH_OTA_DEFERRED: snprintf(b, sizeof b, "v%s prête, après arrêt moteurs", o->remote); break;
    case KH_OTA_FAILED: snprintf(b, sizeof b, "v%s · échec mise à jour v%s", o->running, o->remote); break;
    case KH_OTA_CHECK_FAIL: snprintf(b, sizeof b, "v%s · GitHub injoignable", o->running); break;
    default: snprintf(b, sizeof b, "v%s · MAJ auto avec Wi-Fi", o->running); break;
    }
    set_text(ui.wifi.ota, b);
    set_text_color(ui.wifi.ota, o->state == KH_OTA_DOWNLOADING ? KH_COL_AMBER
                                             : o->state == KH_OTA_FAILED  ? KH_COL_RED
                                                                          : KH_COL_TEXT_DIM,
                                0);
    if (ago >= 0) snprintf(b, sizeof b, "GitHub vérifié il y a %ld s", ago);
    else b[0] = 0;
    set_text(ui.wifi.ota2, b);
}

/* ------------------------------------------------------------------ alerte */
static void alarm_ring_anim(void *obj, int32_t v) { lv_obj_set_style_arc_opa(obj, (lv_opa_t)v, LV_PART_MAIN); }

/* pulsation de l'anneau, seulement pendant que l'alerte est affichée */
static void alarm_ring_start(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.al.ring);
    lv_anim_set_exec_cb(&a, alarm_ring_anim);
    lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&a, 450);
    lv_anim_set_playback_time(&a, 450);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

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
    lv_obj_set_size(dots, 150, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -24);
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
    lv_obj_align(ui.status, LV_ALIGN_TOP_MID, 0, 36);

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
    build_wifi(ui.tile[KH_PAGE_WIFI]);
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
    set_arc_color(a, c, LV_PART_INDICATOR);
}

static void update_motors(const kh_model_t *m, const kh_local_t *loc, int64_t now)
{
    char b[64], v1[16], v2[16];
    float w = 0;
    const float ref = loc->thrust_ref_w > 0 ? loc->thrust_ref_w : 3500.0f;
    bool ok = kh_power_total(m, now, &w);
    if (w < 0) w = 0; /* régénération : jauge à zéro */
    int pm = ok ? (int)lroundf(w / ref * 1000.0f) : 0; /* pour mille */
    int main_v = pm > 1000 ? 1000 : pm, over_v = pm > 1000 ? (pm - 1000 > 1000 ? 1000 : pm - 1000) : 0;
    if (lv_arc_get_value(ui.mot.ring) != main_v) lv_arc_set_value(ui.mot.ring, main_v);
    if (lv_arc_get_value(ui.mot.over) != over_v) lv_arc_set_value(ui.mot.over, over_v);
    set_arc_color(ui.mot.ring, ok ? KH_COL_GREEN : KH_COL_TEXT_DIM, LV_PART_INDICATOR);

    if (ok) snprintf(b, sizeof b, "%d %%", (pm + 5) / 10);
    else snprintf(b, sizeof b, "---");
    set_text(ui.mot.pct, b);
    set_text_color(ui.mot.pct, !ok ? KH_COL_TEXT_DIM : pm > 1000 ? KH_COL_AMBER : KH_COL_TEXT, 0);

    if (ok) {
        fmt_dec(v1, sizeof v1, w / 1000.0f, 2);
        fmt_dec(v2, sizeof v2, ref / 1000.0f, 1);
        snprintf(b, sizeof b, "%s kW / %s kW", v1, v2);
    } else {
        snprintf(b, sizeof b, "puissance inconnue");
    }
    set_text(ui.mot.kw, b);

    char ax[2][16];
    for (int k = 0; k < 2; k++) {
        if (kh_fresh(m->t_power[k], now, KH_STALE_MS)) {
            fmt_dec(v1, sizeof v1, kh_power_axis(m, k) / 1000.0f, 2);
            snprintf(ax[k], sizeof ax[k], "%s", v1);
        } else {
            snprintf(ax[k], sizeof ax[k], "---");
        }
    }
    snprintf(b, sizeof b, "M0 %s · M1 %s kW", ax[0], ax[1]);
    set_text(ui.mot.axes, b);

    for (int k = 0; k < 2; k++) {
        int lvl = kh_vib_level(&m->node[k], now);
        static const char *TXT[] = {"---", "OK", "VIB", "ALARME"};
        snprintf(b, sizeof b, "Ch.%d %s", k, TXT[lvl + 1]);
        set_text(ui.mot.vib[k], b);
        set_text_color(ui.mot.vib[k], level_color(lvl), 0);
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
        set_text(ui.sol.isc[k], b);
        if (ok && n->pv.tc_c100 != KP_I16_INVALID) {
            fmt_dec(v, sizeof v, n->pv.tc_c100 / 100.0f, 1);
            snprintf(b, sizeof b, "%s °C", v);
        } else {
            snprintf(b, sizeof b, "--- °C");
        }
        set_text(ui.sol.tc[k], b);
        if (ok && n->pv.tair_c100 != KP_I16_INVALID && isnan(air)) air = n->pv.tair_c100 / 100.0f;
    }
    if (gn) {
        int g = (int)lroundf(gsum / gn);
        fmt_int(b, sizeof b, g);
        set_text(ui.sol.g, b);
        set_text_color(ui.sol.g, KH_COL_TEXT, 0);
    } else {
        set_text(ui.sol.g, "---");
        set_text_color(ui.sol.g, KH_COL_TEXT_DIM, 0);
    }
    if (!isnan(air)) {
        fmt_dec(v, sizeof v, air, 1);
        snprintf(b, sizeof b, "Air extérieur %s °C", v);
    } else {
        snprintf(b, sizeof b, "Air extérieur --- °C");
    }
    set_text(ui.sol.air, b);
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
            set_bar(ui.vib.bar[k], iv > VIB_MAX ? VIB_MAX : iv);
            fmt_int(v1, sizeof v1, iv);
            snprintf(b, sizeof b, "%s", v1);
        } else {
            set_bar(ui.vib.bar[k], 0);
            snprintf(b, sizeof b, "---");
        }
        set_bg_color(ui.vib.bar[k], c, LV_PART_INDICATOR);
        set_text(ui.vib.val[k], b);
        set_text_color(ui.vib.val[k], lvl > 0 ? c : KH_COL_TEXT, 0);
        if (kh_fresh(n->t_order, now, KH_STALE_MS)) {
            fmt_int(v1, sizeof v1, n->order.amp1p_mg[0]);
            fmt_int(v2, sizeof v2, n->order.amp1p_mg[1]);
            snprintf(b, sizeof b, "1P %s · %s", v1, v2);
        } else {
            b[0] = 0;
        }
        set_text(ui.vib.ord[k], b);
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
        set_text_color(ui.sys.link, KH_COL_AMBER, 0);
    } else if (link) {
        snprintf(b, sizeof b, LV_SYMBOL_WIFI " Liaison OK  %d dBm", m->rssi);
        set_text_color(ui.sys.link, KH_COL_GREEN, 0);
    } else if (loc->binding) {
        snprintf(b, sizeof b, "Recherche du maître…");
        set_text_color(ui.sys.link, KH_COL_AMBER, 0);
    } else {
        snprintf(b, sizeof b, loc->bound ? "Liaison PERDUE" : "Non appairé");
        set_text_color(ui.sys.link, loc->bound ? KH_COL_RED : KH_COL_TEXT_MID, 0);
    }
    set_text(ui.sys.link, b);
    fmt_dec(v, sizeof v, kh_loss_pct(m), 1);
    snprintf(b, sizeof b, "%lu paquets · pertes %s %%", (unsigned long)m->rx_pkts, v);
    set_text(ui.sys.link2, b);

    if (kh_fresh(m->t_sync, now, KH_STALE_MS)) {
        bool utc = m->sync.flags & KP_SYNC_UTC, pps = m->sync.flags & KP_SYNC_PPS;
        if (utc) {
            uint64_t s = m->sync.us / 1000000ull;
            snprintf(b, sizeof b, "GPS %s  %02u:%02u:%02u UTC", pps ? "calé" : "sans PPS", (unsigned)(s / 3600 % 24),
                     (unsigned)(s / 60 % 60), (unsigned)(s % 60));
        } else {
            snprintf(b, sizeof b, "GPS : pas d'heure");
        }
        set_text_color(ui.sys.gps, utc ? (pps ? KH_COL_GREEN : KH_COL_AMBER) : KH_COL_AMBER, 0);
    } else {
        snprintf(b, sizeof b, "GPS : ---");
        set_text_color(ui.sys.gps, KH_COL_TEXT_DIM, 0);
    }
    set_text(ui.sys.gps, b);

    for (int k = 0; k < 2; k++) {
        char t[80];
        lv_color_t c;
        node_text(&m->node[k], now, t, sizeof t, &c);
        snprintf(b, sizeof b, "Ch.%d : %s", k, t);
        set_text(ui.sys.node[k], b);
        set_text_color(ui.sys.node[k], c, 0);
    }

    ui.bound = loc->bound;
    ui.demo = loc->demo;
    set_text(ui.sys.lbl_bind, loc->bound ? LV_SYMBOL_CLOSE " DÉLIER" : LV_SYMBOL_WIFI " LIER");
    set_bg_color(ui.sys.btn_demo, loc->demo ? KH_COL_AMBER : KH_COL_CARD2, 0);
    set_text_color(ui.sys.lbl_demo, loc->demo ? KH_COL_BG : KH_COL_TEXT, 0);
    snprintf(b, sizeof b, LV_SYMBOL_IMAGE " %d %%", loc->brightness_pct);
    set_text(ui.sys.lbl_lum, b);
    if (loc->bound)
        snprintf(b, sizeof b, "maître %02X:%02X:%02X:%02X:%02X:%02X", loc->master_mac[0], loc->master_mac[1],
                 loc->master_mac[2], loc->master_mac[3], loc->master_mac[4], loc->master_mac[5]);
    else
        b[0] = 0;
    if (loc->reset_text[0]) {
        char t[160];
        snprintf(t, sizeof t, "%s\n%s", b, loc->reset_text);
        set_text(ui.sys.mac, t);
    } else {
        set_text(ui.sys.mac, b);
    }
}

static void update_status(const kh_model_t *m, const kh_local_t *loc, int64_t now)
{
    if (loc->demo || (m->simulated && kh_link_ok(m, now))) {
        set_text(ui.status, "SIMULATION");
        set_text_color(ui.status, KH_COL_AMBER, 0);
    } else if (kh_link_ok(m, now)) {
        set_text(ui.status, "LIAISON OK");
        set_text_color(ui.status, KH_COL_GREEN, 0);
    } else {
        set_text(ui.status, loc->bound ? LV_SYMBOL_WARNING " LIAISON PERDUE" : "NON APPAIRÉ");
        set_text_color(ui.status, loc->bound ? KH_COL_RED : KH_COL_TEXT_DIM, 0);
    }
}

static void update_alarm(const kh_model_t *m)
{
    const bool shown = !lv_obj_has_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN);
    if (!m->alarm_pending) {
        if (shown) {
            lv_anim_del(ui.al.ring, alarm_ring_anim);
            lv_obj_add_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    char b[64], v1[16], v2[16];
    snprintf(b, sizeof b, "Chaussette %u", m->alarm_node);
    set_text(ui.al.node, b);
    fmt_int(v1, sizeof v1, m->alarm_last.rms_mg);
    snprintf(b, sizeof b, "%s mg", v1);
    set_text(ui.al.val, b);
    fmt_int(v1, sizeof v1, m->alarm_last.rpm[0]);
    fmt_int(v2, sizeof v2, m->alarm_last.rpm[1]);
    snprintf(b, sizeof b, "M0 %s · M1 %s tr/min", v1, v2);
    set_text(ui.al.rpm, b);
    if (!shown) {
        lv_obj_clear_flag(ui.al.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui.al.root);
        alarm_ring_start();
    }
}

static void update_knob(const kh_knob_view_t *k)
{
    if (!k || !k->valid) {
        if (!lv_obj_has_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(ui.knob_arc, LV_OBJ_FLAG_HIDDEN);
    /* LVGL : 0° à droite, sens horaire ; molette : 0° en haut. Pas de 3° : pas de redessin sur le bruit */
    int a = ((int)lroundf(k->angle_deg / 3.0f)) * 3 + 270;
    a %= 360;
    if (a < 0) a += 360;
    static int last = -1;
    if (a != last) {
        last = a;
        lv_arc_set_angles(ui.knob_arc, (uint16_t)((a + 360 - 7) % 360), (uint16_t)((a + 7) % 360));
    }
    set_arc_color(ui.knob_arc, k->tilt_mode ? KH_COL_AMBER : KH_COL_TEXT_MID, LV_PART_INDICATOR);
}

void kh_screen_update(const kh_model_t *m, const kh_local_t *loc, const kh_knob_view_t *knob, int64_t now)
{
    update_status(m, loc, now);
    /* seule la page visible est mise à jour ; les autres le seront quand on y arrivera */
    switch (kh_screen_page()) {
    case KH_PAGE_MOTORS: update_motors(m, loc, now); break;
    case KH_PAGE_SOLAR: update_solar(m, now); break;
    case KH_PAGE_VIB: update_vib(m, now); break;
    case KH_PAGE_SYSTEM: update_system(m, loc, now); break;
    case KH_PAGE_WIFI: update_wifi(loc, now); break;
    default: break;
    }
    update_alarm(m);
    update_knob(knob);
}
