/*
 * Aperçus sur PC de l'écran énergie 1.64 : compile energie_screen.c avec LVGL en natif, alimente
 * le modèle avec la simulation (même chemin que la radio) et écrit une image par situation.
 *   make -C tools/preview && ./tools/preview/preview_energie <dossier>
 */
#include <stdio.h>
#include <string.h>

#include "energie_screen.h"
#include "kh_model.h"
#include "lvgl.h"

#define W KE_SCR_W
#define H KE_SCR_H
static lv_color_t fb[W * H];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = *c++;
    lv_disp_flush_ready(d);
}

static void dump(const char *dir, const char *name)
{
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            /* coins arrondis de la dalle (rayon ≈ 36 px) */
            int cx = x < 36 ? 36 - x : x > W - 37 ? x - (W - 37) : 0, cy = y < 36 ? 36 - y : y > H - 37 ? y - (H - 37) : 0;
            bool in = cx * cx + cy * cy <= 36 * 36;
            uint16_t px = fb[y * W + x].full;
            unsigned char r = in ? (unsigned char)((px >> 11) << 3) : 40, g = in ? (unsigned char)(((px >> 5) & 63) << 2) : 40,
                          b = in ? (unsigned char)((px & 31) << 3) : 40;
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    fclose(f);
}

static void step(kh_model_t *m, int64_t *t, int ms, const ke_local_t *loc)
{
    for (int i = 0; i < ms / 100; i++) {
        *t += 100;
        khn_frame_t fr[KHN_MAX_FRAMES];
        int n = kh_demo_frames(*t, fr, KHN_MAX_FRAMES);
        uint8_t buf[KHN_PAYLOAD_MAX];
        size_t len = khn_pack(buf, sizeof buf, (uint16_t)(*t / 100), 0, fr, (size_t)n);
        khn_hdr_t h;
        khn_frame_t g[KHN_MAX_FRAMES];
        n = khn_unpack(buf, len, &h, g, KHN_MAX_FRAMES);
        kh_model_apply_packet(m, &h, g, n, -52, *t);
        lv_tick_inc(100);
        ke_screen_update(m, loc, *t);
        lv_timer_handler();
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    lv_init();
    static lv_disp_draw_buf_t db;
    static lv_color_t buf[W * H];
    lv_disp_draw_buf_init(&db, buf, NULL, W * H);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = W;
    dd.ver_res = H;
    dd.flush_cb = flush;
    dd.draw_buf = &db;
    lv_disp_drv_register(&dd);

    ke_screen_init(NULL);
    kh_model_t m;
    kh_model_init(&m);
    ke_local_t loc = {.brightness_pct = 100, .espnow_channel = 1, .thrust_ref_w = 3500, .batt_wh = 4480,
                      .pv_peak_w = 980, .level_w = 2500, .board = "V1.1"};
    loc.peer.paired = loc.peer.alive = true;
    loc.peer.rssi = -38;
    loc.peer.channel = 6;
    memcpy(loc.peer.mac, (uint8_t[]){0x24, 0x58, 0x7C, 0x1A, 0x2B, 0x3C}, 6);
    strcpy(loc.peer.hello.fw, "1.6.0");
    loc.rond.state = KHP_UPD_SENDING;
    loc.rond.pct = 37;
    strcpy(loc.rond.remote, "1.7.0");
    strcpy(loc.ota.running, "1.1.0");
    loc.ota.state = KH_OTA_UP_TO_DATE;
    int64_t t = 0;

    /* moteurs arrêtés au soleil : la batterie se charge */
    kh_demo_set_throttle(0);
    step(&m, &t, 8000, &loc);
    dump(dir, "1_energie_charge");
    /* croisière : la batterie se vide */
    kh_demo_set_throttle(70);
    step(&m, &t, 3000, &loc);
    dump(dir, "2_energie_decharge");
    /* suivi du soleil : bilan nul */
    for (int i = 0; i < 6; i++) { /* consigne recalée sur la production : bilan ≈ 0 */
        kh_energy_cfg_t c = {.batt_wh = 4480, .pv_peak_w = 980, .level_w = 2500};
        kh_energy_t e;
        kh_energy(&m, t, &c, &e);
        kh_demo_set_throttle(e.solar_w / 3500.0f * 100.0f);
        step(&m, &t, 600, &loc);
    }
    dump(dir, "3_energie_equilibre");
    /* détails */
    ke_screen_show_page(KE_PAGE_DETAILS, false);
    step(&m, &t, 500, &loc);
    dump(dir, "4_details");
    /* réglages, Wi-Fi */
    loc.wifi.state = KH_WIFI_CONNECTED;
    strcpy(loc.wifi.ssid, "Atelier-KHEPRI");
    strcpy(loc.wifi.ip, "192.168.1.43");
    loc.wifi.channel = 1;
    loc.wifi.rssi = -55;
    ke_screen_show_page(KE_PAGE_SETTINGS, false);
    step(&m, &t, 500, &loc);
    dump(dir, "5_reglages");
    const char *ss[] = {"Atelier-KHEPRI", "Livebox-4E20", "EAE-Invites", "iPhone de Antoine"};
    const int8_t rs[] = {-55, -63, -70, -48};
    for (int i = 0; i < 4; i++) {
        strcpy(loc.wifi.ap[i].ssid, ss[i]);
        loc.wifi.ap[i].rssi = rs[i];
        loc.wifi.ap[i].secure = i != 2;
    }
    loc.wifi.ap_count = 4;
    loc.wifi.scan_gen = 1;
    ke_screen_debug_wifi(true);
    step(&m, &t, 300, &loc);
    dump(dir, "6_wifi");
    ke_screen_debug_password("Livebox-4E20", "khepri2026");
    lv_timer_handler();
    dump(dir, "7_wifi_mot_de_passe");
    ke_screen_debug_password(NULL, NULL);
    ke_screen_debug_wifi(false);
    /* retour automatique à l'énergie après 10 s */
    ke_screen_show_page(KE_PAGE_DETAILS, false);
    step(&m, &t, 11000, &loc);
    printf("après 11 s sans toucher : page %d (énergie = %d)\n", ke_screen_page(), KE_PAGE_ENERGY);
    /* batterie faible, plein régime */
    kh_model_t low = m;
    low.batt.soc_pm = 120;
    low.batt.e_wh = 538;
    low.t_batt = t;
    ke_screen_update(&low, &loc, t);
    lv_timer_handler();
    dump(dir, "8_batterie_faible");
    /* écran rond perdu */
    loc.peer.alive = false;
    t += 5000;
    lv_tick_inc(5000);
    ke_screen_update(&m, &loc, t);
    lv_timer_handler();
    dump(dir, "9_ecran_rond_perdu");
    return 0;
}
