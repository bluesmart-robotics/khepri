/*
 * kp_frames.h — KHÉPRI : sous-ensemble du protocole CAN des chaussettes utile à l'écran pilote.
 *
 * Copie conforme des trames définies dans khepri_proto/include/kp_can.h (firmware chaussette et
 * maître, protocole v1 avec plages par chaussette). Même encodage octet pour octet : si kp_can.h
 * évolue, mettre à jour ce fichier. Aucune dépendance ESP-IDF (testable sur PC).
 *
 * Plages d'identifiants (trames standard 11 bits, petit-boutiste) :
 *   0x600            SYNC, commune, émise par le maître
 *   0x601 à 0x61F    chaussette 0
 *   0x621 à 0x63F    chaussette 1          → id = 0x600 + 32 × chaussette + décalage
 *   0x000 à 0x03F    axes du BSR Drive (CANSimple, nœud × 32 + commande)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ BSR Drive (ODrive CANSimple) */
#define KP_ODRV_CMD_HEARTBEAT   0x001u
#define KP_ODRV_CMD_ENCODER_EST 0x009u /* float pos [tr], float vel [tr/s] */
#define KP_ODRV_CMD_BUS_VI      0x017u /* float tension de bus [V], float courant de bus [A] (ODrive ≥ 0.6) */
#define KP_ODRV_ID(node, cmd)   ((uint32_t)(((node) << 5) | (cmd)))
#define KP_ODRV_NODE(id)        ((uint8_t)((id) >> 5))
#define KP_ODRV_CMD(id)         ((uint8_t)((id) & 0x1Fu))

/* ------------------------------------------------------------------ chaussettes */
#define KP_ID_BASE   0x600u
#define KP_ID_SYNC   KP_ID_BASE
#define KP_ID_LAST   (KP_ID_BASE + 4u * 32u - 1u)
#define KP_MAX_NODES 2

enum {
    KP_O_VIB_RMS = 0x1,   /* RMS X/Y/Z + crête (int16, mg)                    10 Hz        */
    KP_O_VIB_ORDER = 0x2, /* amplitudes 1P M0/M1, fréquence du pic            10 Hz        */
    KP_O_PV = 0x3,        /* Isc, G, Tc, Tair                                 5 Hz         */
    KP_O_STATUS = 0x4,    /* état, défauts, pertes, durée                     1 Hz         */
    KP_O_ALARM = 0x5,     /* alarme vibratoire                                événement    */
};

static inline uint32_t kp_id(uint8_t node, uint8_t off) { return KP_ID_BASE + 32u * node + off; }
/* chaussette émettrice de la trame, −1 si ce n'est pas une trame de chaussette */
static inline int kp_node_of(uint32_t id)
{
    if (id <= KP_ID_SYNC || id > KP_ID_LAST || (id & 0x1Fu) == 0) return -1;
    return (int)((id - KP_ID_BASE) >> 5);
}
static inline uint8_t kp_off_of(uint32_t id) { return (uint8_t)(id & 0x1Fu); }

/* ------------------------------------------------------------------ bits d'état / défauts */
enum {
    KP_ST_LOGGING = 1u << 0,
    KP_ST_STORAGE_OK = 1u << 1,
    KP_ST_TIME_SYNC = 1u << 2,
    KP_ST_BURST = 1u << 3,
    KP_ST_TRANSFER = 1u << 4,
    KP_ST_ACC_OK = 1u << 5,
    KP_ST_UTC = 1u << 6,
    KP_ST_WIFI = 1u << 7,
};
enum {
    KP_F_ACC = 1u << 0,
    KP_F_RTD_A = 1u << 1,
    KP_F_RTD_B = 1u << 2,
    KP_F_INA = 1u << 3,
    KP_F_STORAGE = 1u << 4,
    KP_F_CAN = 1u << 5,
    KP_F_OVERFLOW = 1u << 6,
    KP_F_LOG_DROP = 1u << 7,
};
enum { KP_SYNC_UTC = 1u << 0, KP_SYNC_PPS = 1u << 1 };
#define KP_SYNC_US_MASK 0x00FFFFFFFFFFFFFFull
#define KP_I16_INVALID  INT16_MAX

/* ------------------------------------------------------------------ petit-boutiste */
static inline void kp_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void kp_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void kp_put_u64(uint8_t *p, uint64_t v) { kp_put_u32(p, (uint32_t)v); kp_put_u32(p + 4, (uint32_t)(v >> 32)); }
static inline uint16_t kp_get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t kp_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t kp_get_u64(const uint8_t *p) { return (uint64_t)kp_get_u32(p) | ((uint64_t)kp_get_u32(p + 4) << 32); }
static inline void kp_put_f32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); kp_put_u32(p, u); }
static inline float kp_get_f32(const uint8_t *p) { uint32_t u = kp_get_u32(p); float f; memcpy(&f, &u, 4); return f; }

/* ------------------------------------------------------------------ trames typées */
typedef struct { uint64_t us; uint8_t flags; } kp_sync_t;
static inline void kp_sync_pack(uint8_t d[8], const kp_sync_t *s)
{
    kp_put_u64(d, (s->us & KP_SYNC_US_MASK) | ((uint64_t)s->flags << 56));
}
static inline void kp_sync_unpack(const uint8_t d[8], kp_sync_t *s)
{
    uint64_t w = kp_get_u64(d);
    s->us = w & KP_SYNC_US_MASK;
    s->flags = (uint8_t)(w >> 56);
}

typedef struct { int16_t rms_mg[3]; int16_t peak_mg; } kp_vib_rms_t;
static inline void kp_vib_rms_pack(uint8_t d[8], const kp_vib_rms_t *v)
{
    for (int i = 0; i < 3; i++) kp_put_u16(d + 2 * i, (uint16_t)v->rms_mg[i]);
    kp_put_u16(d + 6, (uint16_t)v->peak_mg);
}
static inline void kp_vib_rms_unpack(const uint8_t d[8], kp_vib_rms_t *v)
{
    for (int i = 0; i < 3; i++) v->rms_mg[i] = (int16_t)kp_get_u16(d + 2 * i);
    v->peak_mg = (int16_t)kp_get_u16(d + 6);
}

enum { KP_VO_RPM0 = 1u << 0, KP_VO_RPM1 = 1u << 1, KP_VO_WARN = 1u << 2, KP_VO_ALARM = 1u << 3 };
typedef struct { uint16_t amp1p_mg[2]; uint16_t peak_dhz; uint8_t flags; uint8_t seq; } kp_vib_order_t;
static inline void kp_vib_order_pack(uint8_t d[8], const kp_vib_order_t *v)
{
    kp_put_u16(d, v->amp1p_mg[0]); kp_put_u16(d + 2, v->amp1p_mg[1]); kp_put_u16(d + 4, v->peak_dhz);
    d[6] = v->flags; d[7] = v->seq;
}
static inline void kp_vib_order_unpack(const uint8_t d[8], kp_vib_order_t *v)
{
    v->amp1p_mg[0] = kp_get_u16(d); v->amp1p_mg[1] = kp_get_u16(d + 2); v->peak_dhz = kp_get_u16(d + 4);
    v->flags = d[6]; v->seq = d[7];
}

typedef struct { int16_t isc_ma; uint16_t g_wm2; int16_t tc_c100; int16_t tair_c100; } kp_pv_t;
static inline void kp_pv_pack(uint8_t d[8], const kp_pv_t *v)
{
    kp_put_u16(d, (uint16_t)v->isc_ma); kp_put_u16(d + 2, v->g_wm2);
    kp_put_u16(d + 4, (uint16_t)v->tc_c100); kp_put_u16(d + 6, (uint16_t)v->tair_c100);
}
static inline void kp_pv_unpack(const uint8_t d[8], kp_pv_t *v)
{
    v->isc_ma = (int16_t)kp_get_u16(d); v->g_wm2 = kp_get_u16(d + 2);
    v->tc_c100 = (int16_t)kp_get_u16(d + 4); v->tair_c100 = (int16_t)kp_get_u16(d + 6);
}

typedef struct { uint8_t state; uint8_t faults; uint16_t dropped; uint32_t uptime_s; } kp_status_t;
static inline void kp_status_pack(uint8_t d[8], const kp_status_t *s)
{
    d[0] = s->state; d[1] = s->faults; kp_put_u16(d + 2, s->dropped); kp_put_u32(d + 4, s->uptime_s);
}
static inline void kp_status_unpack(const uint8_t d[8], kp_status_t *s)
{
    s->state = d[0]; s->faults = d[1]; s->dropped = kp_get_u16(d + 2); s->uptime_s = kp_get_u32(d + 4);
}

enum { KP_AX_X = 0, KP_AX_Y = 1, KP_AX_Z = 2, KP_AX_VEC = 3 };
typedef struct { uint8_t level; uint8_t axis; uint16_t rms_mg; uint16_t rpm[2]; } kp_alarm_t;
static inline void kp_alarm_pack(uint8_t d[8], const kp_alarm_t *a)
{
    d[0] = a->level; d[1] = a->axis; kp_put_u16(d + 2, a->rms_mg); kp_put_u16(d + 4, a->rpm[0]); kp_put_u16(d + 6, a->rpm[1]);
}
static inline void kp_alarm_unpack(const uint8_t d[8], kp_alarm_t *a)
{
    a->level = d[0]; a->axis = d[1]; a->rms_mg = kp_get_u16(d + 2); a->rpm[0] = kp_get_u16(d + 4); a->rpm[1] = kp_get_u16(d + 6);
}

#ifdef __cplusplus
}
#endif
