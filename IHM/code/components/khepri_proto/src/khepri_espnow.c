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
