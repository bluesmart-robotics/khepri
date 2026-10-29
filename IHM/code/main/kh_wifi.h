/*
 * kh_wifi.h — connexion de l'écran à un réseau Wi-Fi (au sol : mises à jour, outils).
 *
 * La radio est partagée avec ESP-NOW : une fois connecté à un point d'accès, l'ESP32 suit le
 * canal de ce point d'accès. Si ce n'est pas le canal du maître (KHEPRI_ESPNOW_CHANNEL), la
 * liaison avec l'avion est perdue tant que le Wi-Fi reste connecté. Usage prévu : au sol.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KH_WIFI_MAX_AP 12

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
} kh_wifi_ap_t;

typedef enum { KH_WIFI_IDLE = 0, KH_WIFI_CONNECTING, KH_WIFI_CONNECTED, KH_WIFI_FAILED } kh_wifi_state_t;

typedef struct {
    kh_wifi_state_t state;
    char ssid[33];       /* réseau demandé ou connecté */
    char ip[16];
    int8_t rssi;
    uint8_t channel;
    bool saved;          /* identifiants mémorisés en NVS */
    bool scanning;
    int ap_count;
    kh_wifi_ap_t ap[KH_WIFI_MAX_AP];
    uint32_t scan_gen;   /* incrémenté à chaque fin de balayage */
} kh_wifi_status_t;

/* À appeler après l'initialisation du Wi-Fi (khepri_link_init) ; se reconnecte au réseau mémorisé */
void kh_wifi_init(void);
void kh_wifi_scan(void);
void kh_wifi_connect(const char *ssid, const char *password);
void kh_wifi_forget(void);
void kh_wifi_get_status(kh_wifi_status_t *out);
bool kh_wifi_connected(void);

#ifdef __cplusplus
}
#endif
