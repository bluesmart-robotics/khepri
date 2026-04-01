/*
 * main.c — BSR Plug & Play Device Detection
 * Fusion du driver display Waveshare AMOLED SH8601 + detection appareil
 *
 * Base: 07_FactoryProgram/main/main.c (Waveshare)
 * Modif: detection GPIO au boot + interruption a chaud pour switcher
 *        entre role telecommande et role module batterie.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "i2c_bsp.h"
#include "touch_bsp.h"

/* ── BSR includes ── */
#include "device_config.h"
#include "controller_app.h"
#include "battery_app.h"
#include "battery_unplugged_app.h"
#include "bsr_battery_adc.h"
#include "bsr_screen.h"
#include "battery_unplugged_screen.h"

static const char *TAG = "bsr_main";
static SemaphoreHandle_t lvgl_mux = NULL;

/* ── LCD config ── */
#define LCD_HOST  SPI2_HOST

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (-1)

/* Resolution physique du panneau SH8601 (portrait) */
#define PHYS_LCD_W                 280
#define PHYS_LCD_H                 456

/* Resolution logique LVGL par defaut (portrait) */
#define EXAMPLE_LCD_H_RES              PHYS_LCD_W
#define EXAMPLE_LCD_V_RES              PHYS_LCD_H

/* Rotation dynamique : 0=portrait, 90=paysage CW, 180=portrait flip, 270=paysage CCW */
static volatile int s_rotation_deg = 0;

/* Pointeurs globaux pour pouvoir modifier le driver LVGL a chaud */
static lv_disp_drv_t *s_disp_drv_ptr = NULL;
static lv_disp_t     *s_disp_ptr     = NULL;

#define EXAMPLE_USE_TOUCH               1

#define EXAMPLE_LVGL_BUF_HEIGHT        (EXAMPLE_LCD_V_RES / 8)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (16 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     5

static esp_lcd_panel_io_handle_t amoled_bl_handle = NULL;

/* ── LCD init commands SH8601 ── */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 80},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 1},
    {0x63, (uint8_t []){0xFF}, 1, 1},
    {0x51, (uint8_t []){0x00}, 1, 1},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x51, (uint8_t []){0xFF}, 1, 0},
};

/* ══════════════════════════════════════════════
 *  LVGL callbacks (identiques à l'exemple Waveshare)
 * ══════════════════════════════════════════════ */
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                             esp_lcd_panel_io_event_data_t *edata,
                                             void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                                   lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    const int rot = s_rotation_deg;

    if (rot == 0) {
        /* 0° Portrait : flush direct */
        esp_lcd_panel_draw_bitmap(panel_handle,
                                   area->x1 + 0x14, area->y1,
                                   area->x2 + 1 + 0x14, area->y2 + 1,
                                   color_map);
        return;
    }

    const uint32_t area_w = lv_area_get_width(area);
    const uint32_t area_h = lv_area_get_height(area);
    const size_t needed = area_w * area_h;

    static lv_color_t *rot_buf = NULL;
    static size_t rot_buf_size = 0;
    if (rot_buf == NULL || rot_buf_size < needed) {
        if (rot_buf) heap_caps_free(rot_buf);
        rot_buf = heap_caps_malloc(needed * sizeof(lv_color_t), MALLOC_CAP_DMA);
        rot_buf_size = needed;
    }

    int phys_x1, phys_x2, phys_y1, phys_y2;

    if (rot == 90) {
        /* 90° Paysage CW : LVGL 456x280 → panneau 280x456 */
        for (uint32_t y = 0; y < area_h; y++)
            for (uint32_t x = 0; x < area_w; x++)
                rot_buf[x * area_h + (area_h - 1 - y)] = color_map[y * area_w + x];
        phys_x1 = (PHYS_LCD_W - 1) - area->y2;
        phys_x2 = (PHYS_LCD_W - 1) - area->y1;
        phys_y1 = area->x1;
        phys_y2 = area->x2;
    } else if (rot == 180) {
        /* 180° Portrait retourne : LVGL 280x456 → panneau 280x456 (pixels inverses) */
        for (uint32_t y = 0; y < area_h; y++)
            for (uint32_t x = 0; x < area_w; x++)
                rot_buf[(area_h - 1 - y) * area_w + (area_w - 1 - x)] = color_map[y * area_w + x];
        phys_x1 = (PHYS_LCD_W - 1) - area->x2;
        phys_x2 = (PHYS_LCD_W - 1) - area->x1;
        phys_y1 = (PHYS_LCD_H - 1) - area->y2;
        phys_y2 = (PHYS_LCD_H - 1) - area->y1;
    } else {
        /* 270° Paysage CCW : LVGL 456x280 → panneau 280x456 */
        for (uint32_t y = 0; y < area_h; y++)
            for (uint32_t x = 0; x < area_w; x++)
                rot_buf[(area_w - 1 - x) * area_h + y] = color_map[y * area_w + x];
        phys_x1 = area->y1;
        phys_x2 = area->y2;
        phys_y1 = (PHYS_LCD_H - 1) - area->x2;
        phys_y2 = (PHYS_LCD_H - 1) - area->x1;
    }

    esp_lcd_panel_draw_bitmap(panel_handle,
                               phys_x1 + 0x14, phys_y1,
                               phys_x2 + 1 + 0x14, phys_y2 + 1,
                               rot_buf);
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

#if EXAMPLE_USE_TOUCH
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t win = getTouch(&tp_x, &tp_y);
    if (win) {
        /* tp_x = physical row (0..455), tp_y = physical col (0..279) */
        const int rot = s_rotation_deg;
        int lv_max_x, lv_max_y;

        if (rot == 0) {
            data->point.x = tp_y;
            data->point.y = tp_x;
            lv_max_x = PHYS_LCD_W - 1;
            lv_max_y = PHYS_LCD_H - 1;
        } else if (rot == 90) {
            data->point.x = tp_x;
            data->point.y = (PHYS_LCD_W - 1) - tp_y;
            lv_max_x = PHYS_LCD_H - 1;
            lv_max_y = PHYS_LCD_W - 1;
        } else if (rot == 180) {
            data->point.x = (PHYS_LCD_W - 1) - tp_y;
            data->point.y = (PHYS_LCD_H - 1) - tp_x;
            lv_max_x = PHYS_LCD_W - 1;
            lv_max_y = PHYS_LCD_H - 1;
        } else { /* 270 */
            data->point.x = (PHYS_LCD_H - 1) - tp_x;
            data->point.y = tp_y;
            lv_max_x = PHYS_LCD_H - 1;
            lv_max_y = PHYS_LCD_W - 1;
        }

        if (data->point.x < 0) data->point.x = 0;
        if (data->point.y < 0) data->point.y = 0;
        if (data->point.x > lv_max_x) data->point.x = lv_max_x;
        if (data->point.y > lv_max_y) data->point.y = lv_max_y;

        /* Reject (0,0) phantom touches — FT3168 sometimes reports touch count=1
         * with coordinates (0,0) when no finger is present (noise on I2C or
         * internal state not cleared). A real (0,0) touch is extremely unlikely. */
        if (tp_x == 0 && tp_y == 0) {
            data->state = LV_INDEV_STATE_RELEASED;
            static uint32_t s_phantom_cnt = 0;
            s_phantom_cnt++;
            if (s_phantom_cnt % 20 == 1) {
                ESP_LOGW("TOUCH_DBG", "PHANTOM(0,0) #%lu  t=%lldms",
                         (unsigned long)s_phantom_cnt,
                         (long long)(esp_timer_get_time() / 1000));
            }
            return;
        }

        data->state = LV_INDEV_STATE_PRESSED;

        static uint32_t s_touch_cnt = 0;
        s_touch_cnt++;
        /* Log every 10th real touch event */
        if (s_touch_cnt % 10 == 1) {
            ESP_LOGW("TOUCH_DBG", "#%lu  raw=(%d,%d) mapped=(%d,%d)  t=%lldms",
                     (unsigned long)s_touch_cnt,
                     tp_x, tp_y,
                     (int)data->point.x, (int)data->point.y,
                     (long long)(esp_timer_get_time() / 1000));
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/* ── LVGL mutex (thread safety) ── */
bool bsr_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "LVGL mux not initialized");
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void bsr_lvgl_unlock(void)
{
    assert(lvgl_mux && "LVGL mux not initialized");
    xSemaphoreGive(lvgl_mux);
}

/* ── LVGL task ── */
static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (bsr_lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            bsr_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* ── Brightness control ── */
void setBrightnes(uint8_t brig)
{
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t param = brig;
    esp_lcd_panel_io_tx_param(amoled_bl_handle, lcd_cmd, &param, 1);
}

/* ══════════════════════════════════════════════
 *  ROTATION DYNAMIQUE (0°, 90°, 180°, 270°)
 * ══════════════════════════════════════════════ */
void bsr_set_rotation(int deg)
{
    /* Normaliser a 0/90/180/270 */
    deg = ((deg % 360) + 360) % 360;
    if (deg != 0 && deg != 90 && deg != 180 && deg != 270) deg = 0;

    if (s_rotation_deg == deg) return;

    ESP_LOGI(TAG, "Switch display rotation %d°", deg);
    s_rotation_deg = deg;

    if (s_disp_drv_ptr) {
        if (deg == 90 || deg == 270) {
            s_disp_drv_ptr->hor_res = PHYS_LCD_H;  /* 456 */
            s_disp_drv_ptr->ver_res = PHYS_LCD_W;  /* 280 */
        } else {
            s_disp_drv_ptr->hor_res = PHYS_LCD_W;  /* 280 */
            s_disp_drv_ptr->ver_res = PHYS_LCD_H;  /* 456 */
        }
        lv_disp_drv_update(s_disp_ptr, s_disp_drv_ptr);
    }
}

/* Backward compat : landscape=true → 90°, false → 0° */
void bsr_set_landscape_mode(bool landscape)
{
    bsr_set_rotation(landscape ? 90 : 0);
}

/* ══════════════════════════════════════════════
 *  DETECTION APPAREIL PLUG & PLAY
 * ══════════════════════════════════════════════ */

/* Role courant — protege par le fait que seule device_monitor_task le modifie */
static device_role_t s_current_role = DEVICE_ROLE_UNKNOWN;

/* Memorise si on vient du mode batterie (pour continuer en BLE) */
static bool s_was_battery = false;

/* Queue ISR → device_monitor_task */
static QueueHandle_t s_gpio_evt_queue = NULL;

/* Ecran LVGL pour l'etat UNKNOWN (variante "branchez l'appareil") */
static lv_obj_t *s_unknown_screen = NULL;

/* True si battery_unplugged_app est actif (variante UNKNOWN apres batterie) */
static bool s_battery_unplugged_active = false;

/* ISR handler — envoie simplement un evenement dans la queue */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
}

/* Lire les 2 GPIO et retourner le role correspondant
 *
 *   PRESENT(GPIO6)  SELECT(GPIO5)   Resultat
 *   HIGH (1)        x               UNKNOWN (rien branche)
 *   LOW  (0)        LOW  (0)        CONTROLLER (telecommande)
 *   LOW  (0)        HIGH (1)        BATTERY (module batterie)
 */
device_role_t device_read_role(void)
{
    int present = gpio_get_level(DEVICE_PRESENT_GPIO);
    int select  = gpio_get_level(DEVICE_SELECT_GPIO);

    ESP_LOGI(TAG, "GPIO lecture: PRESENT(GPIO%d)=%d  SELECT(GPIO%d)=%d",
             DEVICE_PRESENT_GPIO, present, DEVICE_SELECT_GPIO, select);

    if (present == 1) {
        /* Rien de branche — pull-up actif, personne ne tire a GND */
        return DEVICE_ROLE_UNKNOWN;
    }

    /* Quelque chose est branche (PRESENT = LOW) */
    if (select == 0) {
        return DEVICE_ROLE_CONTROLLER;
    } else {
        return DEVICE_ROLE_BATTERY;
    }
}

/* Configurer les 2 GPIO de detection */
void device_detect_init(void)
{
    /* Les deux GPIO en entree avec pull-up + interruption ANYEDGE */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DEVICE_SELECT_GPIO) | (1ULL << DEVICE_PRESENT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Installer le service ISR GPIO (une seule fois) */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    s_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    assert(s_gpio_evt_queue);

    /* ISR sur les DEUX GPIO — n'importe quel changement declenche la detection */
    ESP_ERROR_CHECK(gpio_isr_handler_add(DEVICE_SELECT_GPIO,
                                          gpio_isr_handler,
                                          (void *)DEVICE_SELECT_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(DEVICE_PRESENT_GPIO,
                                          gpio_isr_handler,
                                          (void *)DEVICE_PRESENT_GPIO));

    ESP_LOGI(TAG, "GPIO %d (SELECT) + GPIO %d (PRESENT) configurees (ANYEDGE + pull-up)",
             DEVICE_SELECT_GPIO, DEVICE_PRESENT_GPIO);
}

/* Afficher l'ecran UNKNOWN — 2 variantes :
 *   - Cas 1 : rien n'etait branche → "Branchez l'appareil"
 *   - Cas 2 : on vient du mode batterie → "Batterie deconnectee (BLE)"
 */
static void show_unknown_screen(void)
{
    ESP_LOGI(TAG, "show_unknown_screen: s_was_battery=%d", s_was_battery);

    if (s_was_battery) {
        /* ── Cas 2 : deconnexion batterie → ecran BMS BLE (paysage) ── */
        battery_unplugged_app_start();
        s_battery_unplugged_active = true;
        return;
    }

    /* ── Cas 1 : rien branche → ecran d'attente (portrait) ── */
    if (bsr_lvgl_lock(-1)) {
        s_unknown_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_unknown_screen, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_unknown_screen, LV_OPA_COVER, 0);

        lv_obj_t *lbl = lv_label_create(s_unknown_screen);
        lv_label_set_text(lbl, "Branchez\nl'appareil");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFBB040), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *sub = lv_label_create(s_unknown_screen);
        lv_label_set_text(sub, "En attente de connexion...");
        lv_obj_set_style_text_color(sub, lv_color_hex(0x7A8099), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);

        lv_scr_load(s_unknown_screen);
        bsr_lvgl_unlock();
    }
}

static void hide_unknown_screen(void)
{
    if (s_battery_unplugged_active) {
        battery_unplugged_app_stop();
        s_battery_unplugged_active = false;
        return;
    }

    if (s_unknown_screen) {
        if (bsr_lvgl_lock(-1)) {
            /* Charger un ecran noir temporaire AVANT de supprimer
             * l'ecran unknown — meme pattern que battery_app_stop */
            lv_obj_t *blank = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(blank, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
            lv_scr_load(blank);
            lv_obj_del(s_unknown_screen);
            s_unknown_screen = NULL;
            bsr_lvgl_unlock();
        }
    }
}

/* Arreter le role courant */
static void stop_current_role(void)
{
    /* Memoriser si on quitte le mode batterie (pour le mode BLE) */
    s_was_battery = (s_current_role == DEVICE_ROLE_BATTERY);

    switch (s_current_role) {
        case DEVICE_ROLE_CONTROLLER:
            controller_app_stop();
            break;
        case DEVICE_ROLE_BATTERY:
            battery_app_stop();
            break;
        case DEVICE_ROLE_UNKNOWN:
            hide_unknown_screen();
            break;
    }
    s_current_role = DEVICE_ROLE_UNKNOWN;
}

/* Demarrer un nouveau role */
void start_role(device_role_t role)
{
    ESP_LOGI(TAG, "Demarrage role : %s", device_role_to_string(role));

    switch (role) {
        case DEVICE_ROLE_CONTROLLER:
            s_was_battery = false;  /* on branche un vrai appareil */
            controller_app_start();
            break;
        case DEVICE_ROLE_BATTERY:
            s_was_battery = false;
            battery_app_start();
            break;
        case DEVICE_ROLE_UNKNOWN:
            show_unknown_screen();
            break;
    }
    s_current_role = role;
}

/* Task de surveillance GPIO — debounce + transition de role */
void device_monitor_task(void *arg)
{
    uint32_t io_num;
    ESP_LOGI(TAG, "device_monitor_task demarre");

    while (1) {
        /* Attendre un evenement GPIO (interruption) */
        if (xQueueReceive(s_gpio_evt_queue, &io_num, portMAX_DELAY)) {
            /* Debounce : attendre la stabilisation */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

            /* Vider les evenements accumules pendant le debounce */
            while (xQueueReceive(s_gpio_evt_queue, &io_num, 0)) {
                /* drain */
            }

            /* Lire le niveau stable */
            device_role_t new_role = device_read_role();

            if (new_role != s_current_role) {
                ESP_LOGI(TAG, "Changement de role detecte : %s → %s",
                         device_role_to_string(s_current_role),
                         device_role_to_string(new_role));

                stop_current_role();
                start_role(new_role);
            } else {
                ESP_LOGD(TAG, "Faux positif GPIO (role inchange : %s)",
                         device_role_to_string(s_current_role));
            }
        }
    }
}

/* ══════════════════════════════════════════════
 *  TACHE BATTERIE LOCALE (ADC)
 * ══════════════════════════════════════════════ */
static void battery_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "battery_monitor_task demarre");
    while (1) {
        uint8_t pct = bsr_battery_get_percent();
        float   volts = bsr_battery_get_voltage();

        /* Mettre a jour l'ecran telecommande si actif */
        if (s_current_role == DEVICE_ROLE_CONTROLLER) {
            bsr_data.batt_pct = pct;
            if (bsr_lvgl_lock(50)) {
                if (bsr_ui.drive.bar_batt_local) {
                    lv_bar_set_value(bsr_ui.drive.bar_batt_local, pct, LV_ANIM_ON);
                    lv_color_t bc = lv_color_hex(0xF2F4F8);  /* white */
                    if (pct <= 20) bc = lv_color_hex(0xF4614A);  /* red */
                    lv_obj_set_style_bg_color(bsr_ui.drive.bar_batt_local, bc, LV_PART_INDICATOR);
                }
                bsr_lvgl_unlock();
            }
        }

        /* Mettre a jour l'ecran unplugged si actif */
        if (s_battery_unplugged_active) {
            if (bsr_lvgl_lock(50)) {
                if (bat_unp_ui.lbl_local_batt) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d%%  %.2fV", pct, volts);
                    lv_label_set_text(bat_unp_ui.lbl_local_batt, buf);

                    lv_color_t bc = lv_color_hex(0x7DD3C8);  /* accent */
                    if (pct < 15) bc = lv_color_hex(0xF4614A);
                    else if (pct < 30) bc = lv_color_hex(0xFBB040);
                    lv_obj_set_style_text_color(bat_unp_ui.lbl_local_batt, bc, 0);
                }
                bsr_lvgl_unlock();
            }
        }

        ESP_LOGD(TAG, "Batterie locale: %d%% (%.2fV)", pct, volts);
        vTaskDelay(pdMS_TO_TICKS(5000));  /* lecture toutes les 5 secondes */
    }
}

/* ══════════════════════════════════════════════
 *  APP_MAIN
 * ══════════════════════════════════════════════ */
void app_main(void)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    /* ════════════════════════════════════
     * PARTIE 1 : Init display AMOLED SH8601
     * (copié tel quel de l'exemple Waveshare)
     * ════════════════════════════════════ */

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        EXAMPLE_PIN_NUM_LCD_PCLK,
        EXAMPLE_PIN_NUM_LCD_DATA0,
        EXAMPLE_PIN_NUM_LCD_DATA1,
        EXAMPLE_PIN_NUM_LCD_DATA2,
        EXAMPLE_PIN_NUM_LCD_DATA3);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        EXAMPLE_PIN_NUM_LCD_CS,
        example_notify_lvgl_flush_ready,
        &disp_drv);

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io_config, &io_handle));
    amoled_bl_handle = io_handle;

    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* I2C + Touch */
    I2C_master_Init();
#if EXAMPLE_USE_TOUCH
    touch_Init();
#endif

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    /* ════════════════════════════════════
     * PARTIE 2 : Init LVGL
     * ════════════════════════════════════ */

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    lv_color_t *buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    disp_drv.rotated = LV_DISP_ROT_NONE;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    s_disp_drv_ptr = &disp_drv;
    s_disp_ptr = disp;

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

#if EXAMPLE_USE_TOUCH
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);
#endif

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE,
                NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    /* ════════════════════════════════════
     * PARTIE 3 : Detection appareil + dispatch
     * ════════════════════════════════════ */

    /* Event loop cree une seule fois (partage entre les roles) */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* NVS init (necessaire pour WiFi/ESP-NOW) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Init ADC batterie locale */
    bsr_battery_adc_init();

    /* Config GPIO de selection d'appareil */
    device_detect_init();

    /* Lecture du role au boot apres stabilisation */
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    device_role_t boot_role = device_read_role();
    ESP_LOGI(TAG, "Role detecte au boot : %s", device_role_to_string(boot_role));

    /* Afficher l'ecran correspondant au role */
    start_role(boot_role);

    /* Lancer la task de surveillance GPIO (detection a chaud) */
    xTaskCreate(device_monitor_task, "dev_mon", 8192, NULL, 3, NULL);

    /* Lancer la task de monitoring batterie locale */
    xTaskCreate(battery_monitor_task, "batt_mon", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "=== BSR Plug & Play ready ===");
}
