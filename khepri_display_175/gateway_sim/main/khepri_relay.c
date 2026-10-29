#include "khepri_relay.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "espnow.h"
#include "espnow_ctrl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kp_frames.h"

static const char *TAG = "khepri_relay";

#define MAX_DISPLAYS 4
#define SLOT_COUNT (1 + KP_MAX_NODES * 5 + 2) /* SYNC, 5 trames par chaussette, 2 axes */
#define ALARM_Q 4

typedef struct {
    bool dirty;
    khn_frame_t f;
} slot_t;

static khepri_relay_cfg_t s_cfg;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static slot_t s_slot[SLOT_COUNT];
static khn_frame_t s_alarm[ALARM_Q];
static int s_alarm_n;
static uint16_t s_seq;
static uint32_t s_sent;
static int s_displays;

static int slot_of(uint32_t id)
{
    if (id == KP_ID_SYNC) return 0;
    int node = kp_node_of(id);
    if (node >= 0) {
        uint8_t off = kp_off_of(id);
        if (node >= KP_MAX_NODES || off < KP_O_VIB_RMS || off > KP_O_STATUS) return -1; /* ALARM traitée à part */
        return 1 + node * 5 + (off - KP_O_VIB_RMS);
    }
    if (id < 0x040 && KP_ODRV_CMD(id) == KP_ODRV_CMD_ENCODER_EST) {
        if (KP_ODRV_NODE(id) == s_cfg.odrv_node_m0) return 1 + KP_MAX_NODES * 5;
        if (KP_ODRV_NODE(id) == s_cfg.odrv_node_m1) return 2 + KP_MAX_NODES * 5;
    }
    return -1;
}

void khepri_relay_push(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    if (dlc > 8) return;
    khn_frame_t f = {.id = (uint16_t)id, .dlc = dlc};
    memcpy(f.data, data, dlc);
    if (kp_node_of(id) >= 0 && kp_off_of(id) == KP_O_ALARM) {
        portENTER_CRITICAL(&s_lock);
        if (s_alarm_n < ALARM_Q) s_alarm[s_alarm_n++] = f;
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    int k = slot_of(id);
    if (k < 0) return;
    portENTER_CRITICAL(&s_lock);
    s_slot[k].f = f;
    s_slot[k].dirty = true;
    portEXIT_CRITICAL(&s_lock);
}

/* ------------------------------------------------------------------ appairage et commandes */
static bool on_bind(espnow_attribute_t attr, uint8_t mac[6], int8_t rssi)
{
    bool ok = attr == KHN_ATTR_INITIATOR;
    ESP_LOGI(TAG, "demande de liaison de " MACSTR " (attribut 0x%04x, %d dBm) : %s", MAC2STR(mac), attr, rssi,
             ok ? "écran KHÉPRI accepté" : "refusée");
    return ok;
}

static void on_ctrl(espnow_attribute_t ini, espnow_attribute_t resp, uint32_t v)
{
    if (ini != KHN_ATTR_INITIATOR) return;
    uint8_t code = khn_cmd_code(v);
    uint32_t arg = khn_cmd_arg(v);
    if (code != KHN_CMD_HEARTBEAT) ESP_LOGI(TAG, "écran → commande %u, argument %lu", code, (unsigned long)arg);
    if (s_cfg.on_cmd) s_cfg.on_cmd(code, arg);
}

/* ------------------------------------------------------------------ émission */
static void relay_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(s_cfg.period_ms));

        khn_frame_t f[KHN_MAX_FRAMES];
        int n = 0;
        portENTER_CRITICAL(&s_lock);
        for (int i = 0; i < s_alarm_n && n < KHN_MAX_FRAMES; i++) f[n++] = s_alarm[i];
        s_alarm_n = 0;
        for (int k = 0; k < SLOT_COUNT && n < KHN_MAX_FRAMES; k++) {
            if (!s_slot[k].dirty) continue;
            f[n++] = s_slot[k].f;
            s_slot[k].dirty = false;
        }
        portEXIT_CRITICAL(&s_lock);
        if (!n) continue;

        uint8_t buf[KHN_PAYLOAD_MAX];
        size_t len = khn_pack(buf, sizeof buf, s_seq++, s_cfg.flags, f, (size_t)n);
        if (!len) continue;

        espnow_ctrl_bind_info_t list[MAX_DISPLAYS];
        size_t cnt = MAX_DISPLAYS;
        if (espnow_ctrl_responder_get_bindlist(list, &cnt) != ESP_OK) cnt = 0;
        int disp = 0;
        for (size_t i = 0; i < cnt; i++) {
            if (list[i].initiator_attribute != KHN_ATTR_INITIATOR) continue;
            disp++;
            espnow_add_peer(list[i].mac, NULL);
            espnow_frame_head_t head = {.broadcast = false, .retransmit_count = 1, .channel = ESPNOW_CHANNEL_CURRENT};
            espnow_send(ESPNOW_DATA_TYPE_DATA, list[i].mac, buf, len, &head, pdMS_TO_TICKS(20));
            s_sent++;
        }
        s_displays = disp;
    }
}

esp_err_t khepri_relay_init(const khepri_relay_cfg_t *cfg)
{
    s_cfg = *cfg;
    if (!s_cfg.period_ms) s_cfg.period_ms = 100;
    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(UINT32_MAX, s_cfg.bind_rssi, on_bind));
    ESP_ERROR_CHECK(espnow_ctrl_responder_data(on_ctrl));
    xTaskCreate(relay_task, "khepri_relay", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "relais vers l'écran pilote prêt (appairage à moins de %d dBm)", s_cfg.bind_rssi);
    return ESP_OK;
}

int khepri_relay_displays(void) { return s_displays; }
uint32_t khepri_relay_sent(void) { return s_sent; }
