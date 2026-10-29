/* Tests sur PC du protocole ESP-NOW et du modèle :  make -C tests && ./tests/test_proto */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kh_model.h"
#include "khepri_espnow.h"

static int g_fail;
#define CHECK(c)                                                   \
    do {                                                           \
        if (!(c)) {                                                \
            printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #c);  \
            g_fail++;                                              \
        }                                                          \
    } while (0)

int main(void)
{
    /* aller-retour d'un lot plein */
    khn_frame_t f[KHN_MAX_FRAMES], g[KHN_MAX_FRAMES];
    memset(f, 0, sizeof f);
    for (int i = 0; i < KHN_MAX_FRAMES; i++) {
        f[i].id = (uint16_t)(0x600 + i);
        f[i].dlc = 8;
        for (int k = 0; k < 8; k++) f[i].data[k] = (uint8_t)(i * 8 + k);
    }
    uint8_t buf[KHN_PAYLOAD_MAX];
    size_t len = khn_pack(buf, sizeof buf, 0xBEEF, KHN_HF_DEMO, f, KHN_MAX_FRAMES);
    CHECK(len > 0 && len <= KHN_PAYLOAD_MAX);
    khn_hdr_t h;
    int n = khn_unpack(buf, len, &h, g, KHN_MAX_FRAMES);
    CHECK(n == KHN_MAX_FRAMES);
    CHECK(h.seq == 0xBEEF && (h.flags & KHN_HF_DEMO));
    for (int i = 0; i < KHN_MAX_FRAMES; i++)
        CHECK(f[i].id == g[i].id && f[i].dlc == g[i].dlc && memcmp(f[i].data, g[i].data, 8) == 0);
    CHECK(khn_pack(buf, sizeof buf, 0, 0, f, KHN_MAX_FRAMES + 1) == 0);
    /* paquets corrompus */
    CHECK(khn_unpack(buf, len - 1, &h, g, KHN_MAX_FRAMES) < 0);
    buf[0] = 'X';
    CHECK(khn_unpack(buf, len, &h, g, KHN_MAX_FRAMES) < 0);

    /* commande 32 bits façon télécommande BSR */
    uint32_t c = khn_cmd(KHN_CMD_MARK, 0x1234);
    CHECK(khn_cmd_code(c) == KHN_CMD_MARK && khn_cmd_arg(c) == 0x1234);

    /* modèle alimenté par la simulation, via le chemin radio complet */
    kh_model_t m;
    kh_model_init(&m);
    kh_demo_set_throttle(80.0f);
    int alarms = 0;
    for (int64_t t = 1000; t < 100000; t += 100) {
        khn_frame_t df[KHN_MAX_FRAMES];
        int nf = kh_demo_frames(t, df, KHN_MAX_FRAMES);
        CHECK(nf > 0 && nf <= KHN_MAX_FRAMES);
        len = khn_pack(buf, sizeof buf, (uint16_t)(t / 100), KHN_HF_DEMO, df, (size_t)nf);
        CHECK(len > 0);
        n = khn_unpack(buf, len, &h, g, KHN_MAX_FRAMES);
        CHECK(n == nf);
        if ((t / 100) % 50 == 7) continue; /* une perte de temps en temps */
        kh_model_apply_packet(&m, &h, g, n, -60, t);
        if (m.alarm_pending) {
            alarms++;
            CHECK(m.alarm_node == 1);
            kh_model_ack_alarm(&m, t);
        }
    }
    CHECK(alarms == 1); /* acquittée puis muette 30 s : une seule alerte pour la rafale */
    CHECK(m.lost_pkts > 0 && kh_loss_pct(&m) < 5.0f);
    float w = 0;
    CHECK(kh_power_total(&m, 99900, &w) && fabsf(w - 2800.0f) < 150.0f); /* 80 % de 3,5 kW */
    CHECK(m.rpm[0] > 1500.0f && m.rpm[1] > 1500.0f);
    CHECK(m.node[0].pv.g_wm2 > 300 && m.node[0].pv.g_wm2 < 1100);
    CHECK(kh_vib_level(&m.node[0], 99900) == 0);
    CHECK(m.sync.flags == (KP_SYNC_UTC | KP_SYNC_PPS));
    CHECK(kh_link_ok(&m, 99900) && !kh_link_ok(&m, 120000));

    /* autonomie : 50 % de 4 000 Wh, 500 W de soleil sur 1 000 W installés, palier à 2 500 W */
    {
        kh_model_t e;
        kh_model_init(&e);
        uint8_t d[8];
        kp_batt_t bt = {.soc_pm = 500, .v_cv = 4600, .i_da = -300, .e_wh = 2000};
        kp_batt_pack(d, &bt);
        kh_model_apply_frame(&e, KP_ID_BATT, d, 8, 1000);
        kp_solar_t so = {.p_w = {260, 240}, .installed_w = 0};
        kp_solar_pack(d, &so);
        kh_model_apply_frame(&e, KP_ID_SOLAR, d, 8, 1000);
        kh_energy_cfg_t c = {.batt_wh = 4000, .pv_peak_w = 1000, .level_w = 2500};
        kh_energy_t r;
        kh_energy(&e, 1500, &c, &r);
        CHECK(r.batt_ok && r.solar_ok && fabsf(r.soc_pct - 50.0f) < 0.01f);
        CHECK(fabsf(r.solar_pct - 50.0f) < 0.01f);
        CHECK(fabsf(r.level_h - 1.0f) < 0.001f);  /* 2 000 Wh / (2 500 − 500) W */
        CHECK(fabsf(r.full_h - 4.0f) < 0.001f);   /* 2 000 Wh manquants / 500 W */
        c.level_w = 400;
        kh_energy(&e, 1500, &c, &r);
        CHECK(isinf(r.level_h));                  /* le soleil suffit au palier */
        kh_energy(&e, 9000, &c, &r);
        CHECK(!r.batt_ok && !r.solar_ok);         /* données périmées */
    }

    printf(g_fail ? "%d échec(s)\n" : "tous les tests passent\n", g_fail);
    return g_fail ? 1 : 0;
}
