/*
 * Aperçus sur PC de l'écran pilote : compile khepri_screen.c avec LVGL en natif, alimente le
 * modèle avec la simulation (même chemin que la radio) et écrit une image PNG par page.
 *   make -C tools/preview && ./tools/preview/preview <dossier>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kh_model.h"
#include "khepri_screen.h"
#include "lvgl.h"

#define W KH_SCR_W
#define H KH_SCR_H
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
            int dx = x - W / 2, dy = y - H / 2;
            bool in = dx * dx + dy * dy <= (W / 2) * (W / 2); /* masque de la dalle ronde */
            uint16_t px = fb[y * W + x].full;
            unsigned char r = in ? (unsigned char)((px >> 11) << 3) : 40, g = in ? (unsigned char)(((px >> 5) & 63) << 2) : 40,
                          b = in ? (unsigned char)((px & 31) << 3) : 40;
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    fclose(f);
}

static void step(kh_model_t *m, int64_t *t, int ms, const kh_local_t *loc, const kh_knob_view_t *k)
{
    for (int i = 0; i < ms / 100; i++) {
        *t += 100;
        khn_frame_t fr[KHN_MAX_FRAMES];
        int n = kh_demo_frames(*t, fr, KHN_MAX_FRAMES);
        uint8_t buf[KHN_PAYLOAD_MAX];
        size_t len = khn_pack(buf, sizeof buf, (uint16_t)(*t / 100), KHN_HF_DEMO, fr, (size_t)n);
        khn_hdr_t h;
        khn_frame_t g[KHN_MAX_FRAMES];
        n = khn_unpack(buf, len, &h, g, KHN_MAX_FRAMES);
        kh_model_apply_packet(m, &h, g, n, -58, *t);
        lv_tick_inc(100);
        kh_screen_update(m, loc, k, *t);
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

    kh_screen_init(NULL);
    kh_model_t m;
    kh_model_init(&m);
    kh_local_t loc = {.bound = true, .brightness_pct = 100, .master_mac = {0x24, 0x58, 0x7C, 0x1A, 0x2B, 0x3C}};
    kh_knob_view_t knob = {.valid = true, .angle_deg = 35.0f};
    int64_t t = 0;

    step(&m, &t, 20000, &loc, &knob);
    const char *names[KH_PAGE_COUNT] = {"1_moteurs", "2_solaire", "3_vibrations", "4_systeme"};
    for (int p = 0; p < KH_PAGE_COUNT; p++) {
        kh_screen_show_page((kh_page_t)p, false);
        knob.angle_deg = 35.0f + 30.0f * p;
        step(&m, &t, 500, &loc, &knob);
        dump(dir, names[p]);
    }
    /* avertissement puis alarme vibratoire de la chaussette 1 */
    kh_screen_show_page(KH_PAGE_VIB, false);
    step(&m, &t, 57500 - (int)t, &loc, &knob);
    dump(dir, "5_vibrations_avertissement");
    step(&m, &t, 61500 - (int)t, &loc, &knob);
    dump(dir, "6_alerte");
    /* liaison perdue */
    kh_model_ack_alarm(&m, t);
    kh_screen_show_page(KH_PAGE_MOTORS, false);
    t += 5000;
    lv_tick_inc(5000);
    kh_screen_update(&m, &loc, NULL, t);
    lv_timer_handler();
    dump(dir, "7_liaison_perdue");
    return 0;
}
