#include "khepri_espnow.h"

#include <string.h>

size_t khn_pack(uint8_t *buf, size_t cap, uint16_t seq, uint8_t flags, const khn_frame_t *f, size_t n)
{
    if (!buf || n > KHN_MAX_FRAMES || cap < KHN_HDR_LEN) return 0;
    size_t need = KHN_HDR_LEN;
    for (size_t i = 0; i < n; i++) {
        if (f[i].dlc > 8) return 0;
        need += 3u + f[i].dlc;
    }
    if (need > cap) return 0;

    buf[0] = KHN_MAGIC0;
    buf[1] = KHN_MAGIC1;
    buf[2] = KHN_VERSION;
    buf[3] = KHN_T_CAN_BATCH;
    buf[4] = (uint8_t)seq;
    buf[5] = (uint8_t)(seq >> 8);
    buf[6] = (uint8_t)n;
    buf[7] = flags;
    size_t p = KHN_HDR_LEN;
    for (size_t i = 0; i < n; i++) {
        buf[p++] = (uint8_t)f[i].id;
        buf[p++] = (uint8_t)((f[i].id >> 8) & 0x07u);
        buf[p++] = f[i].dlc;
        memcpy(buf + p, f[i].data, f[i].dlc);
        p += f[i].dlc;
    }
    return p;
}

int khn_unpack(const uint8_t *buf, size_t len, khn_hdr_t *hdr, khn_frame_t *out, size_t max)
{
    if (!buf || len < KHN_HDR_LEN) return -1;
    if (buf[0] != KHN_MAGIC0 || buf[1] != KHN_MAGIC1 || buf[2] != KHN_VERSION || buf[3] != KHN_T_CAN_BATCH) return -1;
    khn_hdr_t h = {
        .version = buf[2], .type = buf[3], .seq = (uint16_t)(buf[4] | (buf[5] << 8)), .count = buf[6], .flags = buf[7]};
    if (h.count > KHN_MAX_FRAMES) return -1;
    size_t p = KHN_HDR_LEN;
    size_t n = 0;
    for (uint8_t i = 0; i < h.count; i++) {
        if (p + 3 > len) return -1;
        uint16_t id = (uint16_t)(buf[p] | ((buf[p + 1] & 0x07u) << 8));
        uint8_t dlc = buf[p + 2];
        p += 3;
        if (dlc > 8 || p + dlc > len) return -1;
        if (out && n < max) {
            out[n].id = id;
            out[n].dlc = dlc;
            memset(out[n].data, 0, sizeof out[n].data);
            memcpy(out[n].data, buf + p, dlc);
            n++;
        }
        p += dlc;
    }
    if (p != len) return -1;
    if (hdr) *hdr = h;
    return (int)n;
}

/* ------------------------------------------------------------------ liaison entre écrans */
#define KHP_HDR 4 /* 'K' 'Q' version type */

static size_t khp_head(uint8_t *buf, size_t cap, uint8_t type, size_t body)
{
    if (!buf || cap < KHP_HDR + body) return 0;
    buf[0] = KHN_MAGIC0;
    buf[1] = KHP_MAGIC1;
    buf[2] = KHP_VERSION;
    buf[3] = type;
    return KHP_HDR + body;
}

int khp_type(const uint8_t *buf, size_t len)
{
    if (!buf || len < KHP_HDR || buf[0] != KHN_MAGIC0 || buf[1] != KHP_MAGIC1 || buf[2] != KHP_VERSION) return 0;
    return buf[3];
}

static void put_str(uint8_t *d, const char *s, size_t n)
{
    memset(d, 0, n);
    if (s) {
        size_t l = strlen(s);
        memcpy(d, s, l < n - 1 ? l : n - 1);
    }
}
static void get_str(char *s, const uint8_t *d, size_t n)
{
    memcpy(s, d, n);
    s[n - 1] = 0;
}

#define HELLO_BODY (2 + KHP_FW_LEN + 2 + KHP_FW_LEN + KHP_SSID_LEN)
size_t khp_pack_hello(uint8_t *buf, size_t cap, const khp_hello_t *h)
{
    size_t n = khp_head(buf, cap, KHP_T_HELLO, HELLO_BODY);
    if (!n) return 0;
    uint8_t *p = buf + KHP_HDR;
    *p++ = h->role;
    *p++ = h->flags;
    put_str(p, h->fw, KHP_FW_LEN), p += KHP_FW_LEN;
    *p++ = h->upd_state;
    *p++ = h->upd_pct;
    put_str(p, h->upd_ver, KHP_FW_LEN), p += KHP_FW_LEN;
    put_str(p, h->ssid, KHP_SSID_LEN);
    return n;
}
bool khp_unpack_hello(const uint8_t *buf, size_t len, khp_hello_t *h)
{
    if (khp_type(buf, len) != KHP_T_HELLO || len != KHP_HDR + HELLO_BODY) return false;
    const uint8_t *p = buf + KHP_HDR;
    h->role = *p++;
    h->flags = *p++;
    get_str(h->fw, p, KHP_FW_LEN), p += KHP_FW_LEN;
    h->upd_state = *p++;
    h->upd_pct = *p++;
    get_str(h->upd_ver, p, KHP_FW_LEN), p += KHP_FW_LEN;
    get_str(h->ssid, p, KHP_SSID_LEN);
    return true;
}

#define OFFER_BODY (KHP_FW_LEN + 4 + 16)
size_t khp_pack_offer(uint8_t *buf, size_t cap, const khp_offer_t *o)
{
    size_t n = khp_head(buf, cap, KHP_T_OFFER, OFFER_BODY);
    if (!n) return 0;
    uint8_t *p = buf + KHP_HDR;
    put_str(p, o->ver, KHP_FW_LEN), p += KHP_FW_LEN;
    for (int i = 0; i < 4; i++) *p++ = (uint8_t)(o->size >> (8 * i));
    memcpy(p, o->id, 16);
    return n;
}
bool khp_unpack_offer(const uint8_t *buf, size_t len, khp_offer_t *o)
{
    if (khp_type(buf, len) != KHP_T_OFFER || len != KHP_HDR + OFFER_BODY) return false;
    const uint8_t *p = buf + KHP_HDR;
    get_str(o->ver, p, KHP_FW_LEN), p += KHP_FW_LEN;
    o->size = 0;
    for (int i = 0; i < 4; i++) o->size |= (uint32_t)*p++ << (8 * i);
    memcpy(o->id, p, 16);
    return true;
}

size_t khp_pack_answer(uint8_t *buf, size_t cap, const khp_answer_t *a)
{
    size_t n = khp_head(buf, cap, KHP_T_ANSWER, 2);
    if (!n) return 0;
    buf[KHP_HDR] = a->accept;
    buf[KHP_HDR + 1] = a->reason;
    return n;
}
bool khp_unpack_answer(const uint8_t *buf, size_t len, khp_answer_t *a)
{
    if (khp_type(buf, len) != KHP_T_ANSWER || len != KHP_HDR + 2) return false;
    a->accept = buf[KHP_HDR];
    a->reason = buf[KHP_HDR + 1];
    return true;
}
