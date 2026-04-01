/*
 * battery_app.c — Application module batterie robot
 *
 * Utilise battery_screen.c pour afficher l'UI BMS LiFePO4 11S complete
 * (4 pages : Resume, USB-C, Tension pack, Temperatures/MOS).
 *
 * Sera enrichi plus tard avec ESP-NOW en role responder.
 */

#include "battery_app.h"
#include "battery_screen.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "BATTERY_APP";

static bool s_battery_running = false;

extern bool bsr_lvgl_lock(int timeout_ms);
extern void bsr_lvgl_unlock(void);
extern void bsr_set_landscape_mode(bool landscape);

void battery_app_start(void)
{
    if (s_battery_running) {
        ESP_LOGW(TAG, "battery_app deja en cours");
        return;
    }
    ESP_LOGI(TAG, ">>> battery_app_start()");

    /* Passer en mode paysage AVANT de creer l'UI.
     * Note : l'ecran de transition (blank) cree par un _stop precedent
     * reste orphelin — LVGL le reference via disp->prev_scr, on ne peut
     * pas le supprimer sans casser le rendu. Fuite minime (~100 octets). */
    if (bsr_lvgl_lock(-1)) {
        bsr_set_landscape_mode(true);
        battery_screen_init();
        bsr_lvgl_unlock();
    }

    s_battery_running = true;
    ESP_LOGI(TAG, "=== BSR Mode Batterie ready ===");
}

void battery_app_stop(void)
{
    if (!s_battery_running) {
        ESP_LOGW(TAG, "battery_app pas en cours");
        return;
    }
    ESP_LOGI(TAG, ">>> battery_app_stop()");

    /* Nettoyer timers/callbacks AVANT de supprimer l'ecran.
     * IMPORTANT : on charge un ecran noir temporaire AVANT de supprimer
     * l'ecran batterie, sinon la tache LVGL crash (act_scr dangling). */
    if (bsr_lvgl_lock(-1)) {
        battery_screen_cleanup();
        if (battery_ui.screen) {
            /* Ecran de transition noir — sera remplace par le prochain ecran */
            lv_obj_t *blank = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(blank, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
            lv_scr_load(blank);
            /* Maintenant safe de supprimer l'ancien ecran */
            lv_obj_del(battery_ui.screen);
            battery_ui.screen = NULL;
        }
        bsr_set_landscape_mode(false);
        bsr_lvgl_unlock();
    }

    s_battery_running = false;
    ESP_LOGI(TAG, "=== BSR Mode Batterie arrete ===");
}
