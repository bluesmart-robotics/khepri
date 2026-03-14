#include "bsr_screen.h"

// Callback quand un paquet ESP-NOW arrive du robot
void espnow_recv_cb(const uint8_t *mac, const uint8_t *data, int len)
{
    // Décode ton paquet dans bsr_data
    robot_telemetry_t *telem = (robot_telemetry_t *)data;

    bsr_data.speed_kmh       = telem->speed;
    bsr_data.effort_pct      = telem->effort;
    bsr_data.batt_pct        = telem->battery;
    bsr_data.temp_motor_l    = telem->temp_ml;
    bsr_data.temp_motor_r    = telem->temp_mr;
    bsr_data.rssi            = telem->rssi;
    bsr_data.connected       = true;
    bsr_data.latency_ms      = telem->latency;

    // IMPORTANT : mise à jour de l'écran depuis le thread LVGL
    // Option A : si tu es déjà dans le thread LVGL
    bsr_screen_update();

    // Option B : si tu es dans une task FreeRTOS séparée,
    // utilise un timer LVGL ou un mutex :
    // lv_msg_send(MSG_TELEM_UPDATE, NULL);
}