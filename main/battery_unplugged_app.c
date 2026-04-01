/*
 * battery_unplugged_app.c — Application batterie deconnectee (mode BLE)
 *
 * Utilise battery_unplugged_screen.c pour afficher l'UI BMS simplifiee
 * (3 pages : Resume, Tension pack, Temperatures).
 *
 * Detecte l'orientation via l'accelerometre QMI8658 et bascule
 * automatiquement entre les layouts portrait et paysage.
 */

#include "battery_unplugged_app.h"
#include "battery_unplugged_screen.h"
#include "qmi8658.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "BAT_UNP_APP";

static bool s_running = false;
static TaskHandle_t s_orient_task = NULL;

/* Rotation courante de l'ecran (0, 90, 180, 270) */
static int s_rotation_deg = 90;  /* defaut paysage */

/* true si le QMI8658 a ete initialise avec succes */
static bool s_imu_ok = false;

extern bool bsr_lvgl_lock(int timeout_ms);
extern void bsr_lvgl_unlock(void);
extern void bsr_set_rotation(int deg);

/* Recree l'ecran dans la rotation demandee */
static void recreate_screen(int deg)
{
    if (bsr_lvgl_lock(-1)) {
        /* Cleanup timer + ancien ecran */
        bat_unp_screen_cleanup();
        if (bat_unp_ui.screen) {
            lv_obj_t *blank = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(blank, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
            lv_scr_load(blank);
            lv_obj_del(bat_unp_ui.screen);
            bat_unp_ui.screen = NULL;
        }

        /* Changer la rotation display */
        bsr_set_rotation(deg);

        /* Creer le nouvel ecran (paysage pour 90/270, portrait pour 0/180) */
        if (deg == 90 || deg == 270) {
            bat_unp_screen_init();
        } else {
            bat_unp_screen_init_portrait();
        }

        bsr_lvgl_unlock();
    }

    s_rotation_deg = deg;
    ESP_LOGI(TAG, "Ecran recree en rotation %d°", deg);
}

/* Convertit une orientation QMI en degres de rotation ecran */
static int orient_to_deg(qmi8658_orient_t orient)
{
    switch (orient) {
        case QMI_ORIENT_PORTRAIT_0:    return 0;
        case QMI_ORIENT_LANDSCAPE_90:  return 270;
        case QMI_ORIENT_PORTRAIT_180:  return 180;
        case QMI_ORIENT_LANDSCAPE_270: return 90;
        default:                       return -1;
    }
}

/* Task qui poll l'accelerometre et change l'orientation */
static void orientation_task(void *arg)
{
    qmi8658_accel_t accel;
    qmi8658_orient_t last_orient = QMI_ORIENT_LANDSCAPE_90;
    /* Compteur de stabilite pour eviter les changements intempestifs */
    int stable_count = 0;
    qmi8658_orient_t candidate = QMI_ORIENT_UNKNOWN;

    ESP_LOGI(TAG, "orientation_task demarre");

    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(200));

        if (!qmi8658_read_accel(&accel)) {
            continue;
        }

        qmi8658_orient_t orient = qmi8658_get_orientation(&accel);

        /* Ignorer UNKNOWN (zone d'hysteresis ou device a plat) */
        if (orient == QMI_ORIENT_UNKNOWN) {
            stable_count = 0;
            continue;
        }

        /* Meme orientation que l'actuelle → rien a faire */
        if (orient == last_orient) {
            stable_count = 0;
            continue;
        }

        /* Nouvelle orientation detectee → attendre stabilite */
        if (orient == candidate) {
            stable_count++;
        } else {
            candidate = orient;
            stable_count = 1;
        }

        /* Exiger 3 lectures consecutives (~600ms) pour valider */
        if (stable_count >= 3) {
            int want_deg = orient_to_deg(orient);
            if (want_deg >= 0 && want_deg != s_rotation_deg) {
                recreate_screen(want_deg);
            }
            last_orient = orient;
            stable_count = 0;
        }
    }

    ESP_LOGI(TAG, "orientation_task arrete");
    vTaskDelete(NULL);
}

void battery_unplugged_app_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "battery_unplugged_app deja en cours");
        return;
    }
    ESP_LOGI(TAG, ">>> battery_unplugged_app_start()");

    /* Init IMU si pas encore fait */
    if (!s_imu_ok) {
        s_imu_ok = qmi8658_init();
        if (!s_imu_ok) {
            ESP_LOGW(TAG, "QMI8658 non disponible — mode paysage fixe");
        }
    }

    /* Determiner l'orientation initiale */
    s_rotation_deg = 90;  /* defaut paysage */
    if (s_imu_ok) {
        qmi8658_accel_t accel;
        if (qmi8658_read_accel(&accel)) {
            qmi8658_orient_t orient = qmi8658_get_orientation(&accel);
            int deg = orient_to_deg(orient);
            if (deg >= 0) {
                s_rotation_deg = deg;
            }
        }
    }

    /* Creer l'ecran dans l'orientation detectee */
    if (bsr_lvgl_lock(-1)) {
        bsr_set_rotation(s_rotation_deg);
        if (s_rotation_deg == 90 || s_rotation_deg == 270) {
            bat_unp_screen_init();
        } else {
            bat_unp_screen_init_portrait();
        }
        bsr_lvgl_unlock();
    }

    s_running = true;

    /* Lancer la task d'orientation si l'IMU est dispo */
    if (s_imu_ok) {
        xTaskCreate(orientation_task, "orient", 4096, NULL, 2, &s_orient_task);
    }

    ESP_LOGI(TAG, "=== BSR Mode Batterie Unplugged (BLE) ready [%d°] ===",
             s_rotation_deg);
}

void battery_unplugged_app_stop(void)
{
    if (!s_running) {
        ESP_LOGW(TAG, "battery_unplugged_app pas en cours");
        return;
    }
    ESP_LOGI(TAG, ">>> battery_unplugged_app_stop()");

    /* Arreter la task d'orientation en premier */
    s_running = false;
    if (s_orient_task) {
        /* Attendre que la task se termine (max 500ms) */
        vTaskDelay(pdMS_TO_TICKS(300));
        s_orient_task = NULL;
    }

    if (bsr_lvgl_lock(-1)) {
        bat_unp_screen_cleanup();
        if (bat_unp_ui.screen) {
            lv_obj_t *blank = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(blank, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
            lv_scr_load(blank);
            lv_obj_del(bat_unp_ui.screen);
            bat_unp_ui.screen = NULL;
        }
        bsr_set_rotation(0);
        bsr_lvgl_unlock();
    }

    ESP_LOGI(TAG, "=== BSR Mode Batterie Unplugged arrete ===");
}
