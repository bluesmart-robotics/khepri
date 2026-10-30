#include "kh_model.h"

#include <math.h>
#include <string.h>

void kh_model_init(kh_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->odrv_node[0] = 0;
    m->odrv_node[1] = 1;
    m->rssi = -127;
}

static void on_alarm(kh_model_t *m, uint8_t node, const kp_alarm_t *a, int64_t now)
{
    if (a->level < 2) return; /* l'avertissement colore l'écran mais ne fait pas surgir d'alerte */
    if (now < m->node[node].mute_until) return;
    if (m->alarm_pending && m->alarm_node != node && m->alarm_last.level >= a->level) return;
    m->alarm_pending = true;
    m->alarm_node = node;
    m->alarm_last = *a;
    m->alarm_t = now;
}

void kh_model_apply_frame(kh_model_t *m, uint16_t id, const uint8_t *d, uint8_t dlc, int64_t now)
{
    if (id == KP_ID_SYNC) {
        if (dlc < 8) return;
        kp_sync_unpack(d, &m->sync);
        m->t_sync = now;
        return;
    }
    int node = kp_node_of(id);
    if (node >= 0) {
        if (node >= KP_MAX_NODES || dlc < 8) return;
        kh_node_t *n = &m->node[node];
        switch (kp_off_of(id)) {
        case KP_O_VIB_RMS: kp_vib_rms_unpack(d, &n->rms); n->t_rms = now; break;
        case KP_O_VIB_ORDER: kp_vib_order_unpack(d, &n->order); n->t_order = now; break;
        case KP_O_PV: kp_pv_unpack(d, &n->pv); n->t_pv = now; break;
        case KP_O_STATUS: kp_status_unpack(d, &n->status); n->t_status = now; break;
        case KP_O_ALARM:
            kp_alarm_unpack(d, &n->alarm);
            n->t_alarm = now;
            on_alarm(m, (uint8_t)node, &n->alarm, now);
            break;
        default: break;
        }
        return;
    }
    if (id == KP_ID_BATT && dlc >= 8) {
        kp_batt_unpack(d, &m->batt);
        m->t_batt = now;
        return;
    }
    if (id == KP_ID_SOLAR && dlc >= 8) {
        kp_solar_unpack(d, &m->solar);
        m->t_solar = now;
        return;
    }
    if (id < 0x040 && KP_ODRV_CMD(id) == KP_ODRV_CMD_BUS_VI && dlc >= 8) {
        uint8_t ax = KP_ODRV_NODE(id);
        for (int k = 0; k < 2; k++) {
            if (ax == m->odrv_node[k]) {
                m->vbus[k] = kp_get_f32(d);
                m->ibus[k] = kp_get_f32(d + 4);
                m->t_power[k] = now;
            }
        }
        return;
    }
    if (id < 0x040 && KP_ODRV_CMD(id) == KP_ODRV_CMD_ENCODER_EST && dlc >= 8) {
        uint8_t ax = KP_ODRV_NODE(id);
        for (int k = 0; k < 2; k++) {
            if (ax == m->odrv_node[k]) {
                m->rpm[k] = kp_get_f32(d + 4) * 60.0f;
                m->t_motor[k] = now;
            }
        }
    }
}

void kh_model_apply_packet(kh_model_t *m, const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi, int64_t now)
{
    if (m->seq_valid) {
        uint16_t gap = (uint16_t)(h->seq - m->last_seq);
        if (gap > 1 && gap < 1000) m->lost_pkts += gap - 1u;
    }
    m->last_seq = h->seq;
    m->seq_valid = true;
    m->rx_pkts++;
    m->t_rx = now;
    m->rssi = rssi;
    m->simulated = (h->flags & KHN_HF_DEMO) != 0;
    for (int i = 0; i < n; i++) kh_model_apply_frame(m, f[i].id, f[i].data, f[i].dlc, now);
}

void kh_model_ack_alarm(kh_model_t *m, int64_t now)
{
    if (!m->alarm_pending) return;
    m->node[m->alarm_node].mute_until = now + KH_ALARM_MUTE_MS;
    m->alarm_pending = false;
}

float kh_vib_vec_mg(const kp_vib_rms_t *r)
{
    float s = 0;
    for (int i = 0; i < 3; i++) {
        if (r->rms_mg[i] == KP_I16_INVALID) return NAN;
        s += (float)r->rms_mg[i] * (float)r->rms_mg[i];
    }
    return sqrtf(s);
}

int kh_vib_level(const kh_node_t *n, int64_t now)
{
    if (kh_fresh(n->t_order, now, KH_STALE_MS)) {
        if (n->order.flags & KP_VO_ALARM) return 2;
        if (n->order.flags & KP_VO_WARN) return 1;
    }
    if (!kh_fresh(n->t_rms, now, KH_STALE_MS)) return -1;
    float v = kh_vib_vec_mg(&n->rms);
    if (isnan(v)) return -1;
    return v >= KH_VIB_ALARM_MG ? 2 : v >= KH_VIB_WARN_MG ? 1 : 0;
}

bool kh_power_total(const kh_model_t *m, int64_t now, float *w)
{
    float s = 0;
    bool any = false;
    for (int k = 0; k < 2; k++) {
        if (!kh_fresh(m->t_power[k], now, KH_STALE_MS)) continue;
        float p = kh_power_axis(m, k);
        if (p == p) { /* pas NaN */
            s += p;
            any = true;
        }
    }
    *w = s;
    return any;
}

float kh_loss_pct(const kh_model_t *m)
{
    uint32_t tot = m->rx_pkts + m->lost_pkts;
    return tot ? 100.0f * (float)m->lost_pkts / (float)tot : 0.0f;
}

/* ------------------------------------------------------------------ simulation */
void kh_energy(const kh_model_t *m, int64_t now, const kh_energy_cfg_t *cfg, kh_energy_t *o)
{
    memset(o, 0, sizeof *o);
    o->level_h = NAN;
    o->full_h = NAN;
    o->solar_ok = kh_fresh(m->t_solar, now, KH_STALE_MS);
    if (o->solar_ok) {
        o->solar_w = (float)m->solar.p_w[0] + (float)m->solar.p_w[1];
        float peak = m->solar.installed_w ? (float)m->solar.installed_w : cfg->pv_peak_w;
        o->solar_pct = peak > 0 ? 100.0f * o->solar_w / peak : 0;
        if (o->solar_pct > 100.0f) o->solar_pct = 100.0f;
    }
    o->batt_ok = kh_fresh(m->t_batt, now, KH_STALE_MS) && m->batt.soc_pm <= 1000;
    if (!o->batt_ok) return;
    o->soc_pct = m->batt.soc_pm / 10.0f;
    /* énergie restante : celle du BMS si elle est donnée, sinon SOC × capacité */
    float e = m->batt.e_wh ? (float)m->batt.e_wh : o->soc_pct / 100.0f * cfg->batt_wh;
    float full = o->soc_pct >= 5.0f && m->batt.e_wh ? e * 100.0f / o->soc_pct : cfg->batt_wh;
    float sol = o->solar_ok ? o->solar_w : 0;
    float need = cfg->level_w - sol; /* ce que la batterie doit fournir en palier */
    o->level_h = need <= 0 ? INFINITY : e / need;
    if (o->soc_pct >= 99.5f) o->full_h = 0;
    else if (sol >= 5.0f) o->full_h = (full - e > 0 ? full - e : 0) / sol;
}

static void put(khn_frame_t *f, uint16_t id, const uint8_t d[8])
{
    f->id = id;
    f->dlc = 8;
    memcpy(f->data, d, 8);
}

#define DEMO_REF_W 3500.0f
static float s_demo_cmd = 0.0f;  /* consigne [%] */
static float s_demo_p = 0.0f;    /* puissance simulée totale [W] */
static int64_t s_demo_t = 0;
#define DEMO_BATT_WH 4480.0f   /* 2 × 7S5P EVE LFP */
#define DEMO_PV_PEAK_W 980.0f  /* 288 cellules Maxeon Gen III ≈ 3,4 W */
static float s_demo_e = 0.78f * DEMO_BATT_WH;

void kh_demo_set_throttle(float pct) { s_demo_cmd = pct < 0 ? 0 : pct; }

int kh_demo_frames(int64_t now, khn_frame_t *out, int max)
{
    if (max < 17) return 0;
    const float t = (float)now / 1000.0f;
    int n = 0;
    uint8_t d[8];

    /* SYNC : heure GPS calée (UTC + PPS) */
    kp_sync_t s = {.us = (uint64_t)now * 1000ull + 1793210000000000ull, .flags = KP_SYNC_UTC | KP_SYNC_PPS};
    kp_sync_pack(d, &s);
    put(&out[n++], KP_ID_SYNC, d);

    /* Puissance : suit la consigne (premier ordre, ~0,5 s) ; régime ≈ racine cubique de la puissance */
    float dt = s_demo_t ? (float)(now - s_demo_t) : 0.0f;
    s_demo_t = now;
    float target = s_demo_cmd / 100.0f * DEMO_REF_W;
    float a1 = dt / 500.0f; /* les moteurs répondent en ≈ 0,5 s */
    s_demo_p += (target - s_demo_p) * (a1 > 1.0f ? 1.0f : a1);
    float load = s_demo_p / DEMO_REF_W;
    float rpm[2];
    for (int k = 0; k < 2; k++)
        rpm[k] = load > 0.01f ? 2400.0f * cbrtf(load) + 15.0f * sinf(t * 1.3f + k) : 0.0f;
    for (int k = 0; k < 2; k++) {
        kp_put_f32(d, t * rpm[k] / 60.0f);
        kp_put_f32(d + 4, rpm[k] / 60.0f);
        put(&out[n++], (uint16_t)KP_ODRV_ID(k, KP_ODRV_CMD_ENCODER_EST), d);
        /* Puissance : croisière vers 2,6 kW au total, montées au-delà de 3,5 kW (> 100 %) */
        float p = s_demo_p * (k == 0 ? 0.51f : 0.49f) + (s_demo_p > 20.0f ? 15.0f * sinf(t * 0.9f + k) : 0.0f);
        float v = 47.5f - p * 0.0008f; /* 14S LFP, légère chute sous charge */
        kp_put_f32(d, v);
        kp_put_f32(d + 4, p / v);
        put(&out[n++], (uint16_t)KP_ODRV_ID(k, KP_ODRV_CMD_BUS_VI), d);
    }

    /* Énergie : le soleil (même ensoleillement que les cellules témoins) recharge, les moteurs vident */
    float g[2];
    for (int k = 0; k < 2; k++)
        g[k] = 840.0f + 120.0f * sinf(t * 0.05f + k * 1.3f) - (fmodf(t + k * 7.0f, 45.0f) < 6.0f ? 380.0f : 0.0f);
    float psol[2];
    for (int k = 0; k < 2; k++) psol[k] = g[k] / 1000.0f * DEMO_PV_PEAK_W / 2.0f * 0.85f;
    float net = psol[0] + psol[1] - s_demo_p; /* + = charge */
    s_demo_e += net * dt / 3.6e6f;
    if (s_demo_e > DEMO_BATT_WH) s_demo_e = DEMO_BATT_WH;
    if (s_demo_e < 0) s_demo_e = 0;
    float soc = s_demo_e / DEMO_BATT_WH;
    float vb = 44.8f + 2.0f * soc - s_demo_p * 0.0008f;
    kp_batt_t bt = {.soc_pm = (uint16_t)lroundf(soc * 1000.0f), .v_cv = (uint16_t)lroundf(vb * 100.0f),
                    .i_da = (int16_t)lroundf(net / vb * 10.0f), .e_wh = (uint16_t)lroundf(s_demo_e)};
    kp_batt_pack(d, &bt);
    put(&out[n++], KP_ID_BATT, d);
    kp_solar_t so = {.p_w = {(uint16_t)lroundf(psol[0]), (uint16_t)lroundf(psol[1])}, .installed_w = (uint16_t)DEMO_PV_PEAK_W,
                     .flags = KP_SOL_MPPT0 | KP_SOL_MPPT1};
    kp_solar_pack(d, &so);
    put(&out[n++], KP_ID_SOLAR, d);

    /* Alarme de démonstration : chaussette 1, 4 s toutes les 90 s */
    const float cyc = fmodf(t, 90.0f);
    const bool alarm1 = cyc > 60.0f && cyc < 64.0f;
    const bool warn1 = cyc > 55.0f && cyc < 68.0f;

    for (uint8_t node = 0; node < 2; node++) {
        const float ph = node * 1.3f;
        float base = 250.0f + (node == 0 ? 1100.0f : 1300.0f) * load;
        if (node == 1 && warn1) base = alarm1 ? 6600.0f : 3600.0f;

        kp_vib_rms_t r = {.rms_mg = {(int16_t)(base * 0.55f + 90.0f * sinf(t * 1.7f + ph)),
                                     (int16_t)(base * 0.70f + 120.0f * sinf(t * 1.1f + ph)),
                                     (int16_t)(base * 0.45f + 60.0f * sinf(t * 2.3f + ph))},
                          .peak_mg = (int16_t)(base * 2.9f)};
        kp_vib_rms_pack(d, &r);
        put(&out[n++], (uint16_t)kp_id(node, KP_O_VIB_RMS), d);

        float vec = kh_vib_vec_mg(&r);
        kp_vib_order_t o = {.amp1p_mg = {(uint16_t)(vec * 0.42f), (uint16_t)(vec * 0.37f)},
                            .peak_dhz = (uint16_t)(rpm[node] / 60.0f * 10.0f),
                            .flags = (uint8_t)(KP_VO_RPM0 | KP_VO_RPM1 | (vec >= KH_VIB_WARN_MG ? KP_VO_WARN : 0) |
                                               (vec >= KH_VIB_ALARM_MG ? KP_VO_ALARM : 0)),
                            .seq = (uint8_t)(now / 100)};
        kp_vib_order_pack(d, &o);
        put(&out[n++], (uint16_t)kp_id(node, KP_O_VIB_ORDER), d);

        /* Soleil : nuages lents ; Isc = G × Isc_STC / 1000 (Isc_STC = 6,3 A) */
        kp_pv_t pv = {.isc_ma = (int16_t)(g[node] * 6.3f),
                      .g_wm2 = (uint16_t)g[node],
                      .tc_c100 = (int16_t)((38.0f + 0.012f * g[node] + node * 1.5f) * 100.0f),
                      .tair_c100 = (int16_t)((16.5f + 0.4f * sinf(t * 0.01f)) * 100.0f)};
        kp_pv_pack(d, &pv);
        put(&out[n++], (uint16_t)kp_id(node, KP_O_PV), d);

        kp_status_t st = {.state = KP_ST_LOGGING | KP_ST_STORAGE_OK | KP_ST_TIME_SYNC | KP_ST_ACC_OK | KP_ST_UTC,
                          .faults = 0,
                          .dropped = 0,
                          .uptime_s = (uint32_t)(now / 1000) + 1200u};
        kp_status_pack(d, &st);
        put(&out[n++], (uint16_t)kp_id(node, KP_O_STATUS), d);

        if (node == 1 && alarm1) {
            kp_alarm_t a = {.level = 2, .axis = KP_AX_VEC, .rms_mg = (uint16_t)vec, .rpm = {(uint16_t)rpm[0], (uint16_t)rpm[1]}};
            kp_alarm_pack(d, &a);
            put(&out[n++], (uint16_t)kp_id(node, KP_O_ALARM), d);
        }
    }
    return n;
}
