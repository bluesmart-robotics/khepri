/**
 *________________________________________________________________________
 *
 *       BSR ESP-NOW DEMO Project BY BEENETIC Systems SAS
 * ________________________________________________________________________
 *
 *     Copyright (c) 2022 BENEETIC Systems SAS . All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @file bsr_espnow_control.c
 * @brief ESP-NOW remote control demo for BSR, which demonstrates how to use ESP-NOW to implement a simple remote control application, where one device (initiator) can send control data to another device (responder) when buttons are pressed, and the responder can process the received control data and execute corresponding actions.
 * @author Laurent MARCHADIER - BEENETIC Systems SAS
 * @contact: laurent.marchadier@beenetic.fr
 * @version 1.0
 * ________________________________________________________________________
*/

/*
Voici une explication concise de chaque champ de cette configuration par défaut ESP‑NOW (dans le contexte de l’exemple « esp-now » d’Espressif).

#define ESPNOW_INIT_CONFIG_DEFAULT()
{ 
    .pmk = "ESP_NOW", 
    .forward_enable = 1, 
    .forward_switch_channel = 0, 
    .sec_enable = 0, 
    .reserved1 = 0,   
    .qsize = 32, 
    .send_retry_num = 10, 
    .send_max_timeout = pdMS_TO_TICKS(3000),
    .receive_enable = { 
                .ack           = 1, 
                .forward       = 1, 
                .group         = 1, 
                .provisioning   = 0, 
                .control_bind  = 0, 
                .control_data  = 0, 
                .ota_status    = 0, 
                .ota_data      = 0, 
                .debug_log     = 0, 
                .debug_command = 0, 
                .data          = 0, 
                .sec_status    = 0, 
                .sec           = 0, 
                .sec_data      = 0, 
                .timesync      = 0, 
                .reserved2     = 0, 
                }, \
    }

### Champs principaux
____________________________________________
pmk

Clé maîtresse principale (Primary Master Key) utilisée pour chiffrer les communications ESP‑NOW lorsque le chiffrement est activé.  
Ici, `"ESP_NOW"` est juste une valeur par défaut, à remplacer par une chaîne secrète commune à tous les nœuds si tu actives la sécurité.  
Elle doit être identique sur tous les appareils d’un même réseau sécurisé pour que les paquets chiffrés soient acceptés.

____________________________________________
forward_enable

Active ou non le rôle de « routeur/relai » ESP‑NOW sur ce nœud. 
Quand il est à `1`, le device peut recevoir des messages et les retransmettre à d’autres nœuds (topologie maillée plutôt que strictement point‑à‑point).  
Utile pour étendre la portée ou relier des nœuds qui ne sont pas en portée radio directe.

____________________________________________
forward_switch_channel 

Indique si le nœud a le droit de changer de canal Wi‑Fi lorsqu’il fait du forwarding.  
À `0`, il reste sur son canal actuel, ce qui simplifie le réseau mais limite le relais entre canaux différents.  
À `1`, il peut basculer de canal pour relayer des paquets venant de nœuds sur d’autres canaux (au prix de plus de complexité et de latence).

____________________________________________
sec_enable

Active ou non le mécanisme de sécurité/chiffrement ESP‑NOW.  
À `0`, les paquets sont envoyés en clair (plus simple pour les tests, mais moins sûr).  
À `1`, les paquets peuvent être chiffrés, en combinaison avec la PMK et les clés des pairs.

____________________________________________
reserved1

Champ de réserve pour des évolutions futures ou un usage interne.  
À laisser à `0` pour garantir la compatibilité avec la pile ESP‑NOW actuelle.  
Ne le modifie pas tant que la documentation ne l’indique pas explicitement.

____________________________________________
qsize

Taille de la file de messages (queue) utilisée pour buffered les paquets envoyés/réceptionnés par ESP‑NOW.  
Une valeur plus grande permet de tolérer des rafales de messages ou des traitements plus lents côté application.  
Une valeur trop grande consomme plus de mémoire RAM, une valeur trop petite peut engendrer des pertes de messages.

____________________________________________
send_retry_num

Nombre de tentatives de ré‑envoi pour un message ESP‑NOW en cas d’échec.  
Plus ce nombre est élevé, plus tu donnes de chances à un paquet d’atteindre son destinataire malgré des pertes radio.  
En contrepartie, tu peux augmenter la latence globale et la charge radio si le lien est mauvais.
____________________________________________

send_max_timeout  
Durée maximale (en ticks FreeRTOS) pendant laquelle l’API d’envoi peut attendre (ou considérer des réessais) avant de considérer l’opération comme un échec.  
Ici, `pdMS_TO_TICKS(3000)` correspond à 3 secondes converties en ticks système.  
Réduire cette valeur rend l’échec plus rapide, mais peut être trop agressif si le lien est instable.
____________________________________________

### Sous-structure receive_enable

Cette sous-structure contrôle quels types de trames ou canaux logiques ESP‑NOW ce nœud accepte/traite.  
Chaque champ vaut `1` pour « autorisé/activé », `0` pour « ignoré/désactivé ».  
L’idée est de filtrer par type de trafic (ack, data, debug, OTA, etc.) pour ne gérer que ce qui t’intéresse.

- **ack**  
  Active la réception et le traitement des trames d’acquittement (acknowledgement) ESP‑NOW.  
  Indispensable si tu comptes utiliser les confirmations d’envoi et les statistiques de livraison.  
  Sans ack, tu ne sauras pas si les paquets ont vraiment été reçus.

- **forward**  
  Active la réception des paquets devant être relayés (trafic de forwarding).  
  Nécessaire si le nœud joue un rôle de routeur ESP‑NOW.  
  Couplé à `forward_enable`, cela permet à la carte de recevoir et retransmettre ce type de messages.

- **group**  
  Active la réception de paquets marqués comme « group » (destinés à un groupe de nœuds).  
  Utile pour mettre en place des messages de diffusion logique (multicast) à un groupe d’appareils.  
  Si tu ne fais que du un‑à‑un, tu peux le laisser à 0.

- **provisioning**  
  Active la réception de trames dédiées au « provisioning » (configuration initiale/association d’un nouvel appareil).  
  Utilisé lorsque tu as un schéma où un contrôleur configure dynamiquement les nœuds (ID, clés, etc.).  
  À 0 si tu n’utilises pas ce mode d’appairage/provisioning avancé.

- **control_bind**  
  Permet de recevoir des trames de type « binding » de contrôle (liaison logique entre contrôleur et périphérique).  
  Utile dans des scénarios où un bouton se lie à une lampe, un capteur à une passerelle, etc.  
  Inutile dans un simple échange point‑à‑point statique.

- **control_data**  
  Active la réception de données de contrôle (commandes, ordres, messages de management applicatif).  
  Utile si tu as un canal dédié pour les commandes (à distinguer des données « normales »).  
  À 0 dans la config par défaut si tu n’utilises pas ce sous‑canal.

- **ota_status**  
  Active la réception des messages de statut liés à des mises à jour OTA (Over‑The‑Air).  
  Ces trames servent par exemple à remonter la progression ou le résultat d’une mise à jour de firmware.  
  À activer uniquement si tu implémentes un mécanisme OTA basé sur ESP‑NOW.

- **ota_data**  
  Active la réception des données de firmware OTA envoyées via ESP‑NOW.  
  Nécessaire pour un système où le firmware est distribué directement par ce protocole.  
  Laisser à 0 si tu ne fais pas d’OTA via ESP‑NOW.

- **debug_log**  
  Active les messages de log/debug transportés via ESP‑NOW.  
  Utile pour remonter des logs à un collecteur central sans Wi‑Fi « normal ».  
  À 0 en prod pour éviter de saturer le réseau radio.

- **debug_command**  
  Active la réception de commandes de debug (par exemple forcer un reboot, changer un paramètre, etc.).  
  Permet de piloter le nœud à distance pour des diagnostics.  
  À désactiver en production si tu n’en as pas besoin pour des raisons de sécurité.

- **data**  
  Active la réception de données applicatives « normales ».  
  C’est souvent le type de message principal pour ton application (mesures de capteurs, commandes utilisateur, etc.).  
  À 0 dans cette config par défaut d’exemple, car l’exemple peut router ces types via d’autres flags ou ne s’en servir qu’en émission dans un premier temps.

- **sec_status**  
  Active la réception de trames de statut liées à la sécurité (clé, état des sessions sécurisées, etc.).  
  Utile pour gérer le cycle de vie des clés et les erreurs de chiffrement.  
  Reste à 0 si tu travailles en mode non sécurisé.

- **sec**  
  Active la réception de trames de contrôle de sécurité (par exemple distribution de clés, négociation).  
  Nécessaire pour des scénarios complexes de gestion de clés dynamiques.  
  À 0 pour un usage minimaliste ou purement non chiffré.

- **sec_data**  
  Active la réception de données applicatives spécifiquement marquées comme sécurisées.  
  Tu l’utiliseras si tu distingues explicitement « data non sécurisée » et « data sécurisée » dans ton protocole applicatif.  
  À 0 tant que tu n’as pas mis en place cette différenciation.

- **timesync**  
  Active les messages de synchronisation temporelle entre nœuds (time sync).  
  Permet de synchroniser des horloges locales pour des actions simultanées, horodatage cohérent, etc.  
  À 0 si tu n’utilises pas la synchronisation de temps via ESP‑NOW.

- **reserved2**  
  Champ de réserve pour usages futurs.  
  À laisser à 0 pour assurer la compatibilité.  
  Ne pas utiliser sans indication explicite de la doc.

*/


#include "bsr_esp_now_control.h"
#include "bsr_screen.h"

static const char *TAG = "BSR_ESPNOW_DEMO";

// Global variable to store the current status of the ESP‑NOW control, which is used to determine whether the initiator can send control data to the responder or not.
// The initiator can only send control data to the responder when the devices are in bound status, which means the binding process has been completed successfully.
// Otherwise, the initiator will not send any control data to the responder and will log a message to remind the user to bind the devices firstly.
typedef enum {
    APP_ESPNOW_CTRL_INIT,
    APP_ESPNOW_CTRL_BOUND,
    APP_ESPNOW_CTRL_MAX
} app_espnow_ctrl_status_t;

// The global variable to store the current status of the ESP‑NOW control, which is initialized to APP_ESPNOW_CTRL_INIT, meaning that the devices are not bound yet and the initiator cannot send control data to the responder.
static app_espnow_ctrl_status_t s_espnow_ctrl_status = APP_ESPNOW_CTRL_INIT;

/***********************************************************************************
 * @brief Convert the bind error type to string for logging and debugging.
 *
 * @param[in] espnow_ctrl_bind_error_t bind_error the error type of bind failure, which is set by library when bind failed.
 * @return char * the string description of the bind error, which is used for logging and debugging.
 ***********************************************************************************/
static char *bind_error_to_string(espnow_ctrl_bind_error_t bind_error)
{
    switch (bind_error) {
    case ESPNOW_BIND_ERROR_NONE: {
        return "No error";
        break;
    }

    case ESPNOW_BIND_ERROR_TIMEOUT: {
        return "bind timeout";
        break;
    }

    case ESPNOW_BIND_ERROR_RSSI: {
        return "bind packet RSSI below expected threshold";
        break;
    }

    case ESPNOW_BIND_ERROR_LIST_FULL: {
        return "bindlist is full";
        break;
    }

    default: {
        return "unknown error";
        break;
    }
    }
}

/***********************************************************************************
 * @brief Initialize Wi-Fi in station mode, which is necessary for ESP‑NOW to operate.
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_wifi_init()
{
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button is pressed.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] bool new_status the new status of the button, which is toggled in each button press callback, and is used to indicate whether the button is pressed or released.
 * @param[in] int button_id the ID of the button.
 * @param[in] char button_name the name of the button.
 * @return bool the new status of the button, which is toggled in each button press callback, and is used to indicate whether the button is pressed or released.
 ***********************************************************************************/
static bool bsr_initiator_send_data(void *arg, bool new_status, uint32_t button_id, char button_name)
{
    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == iot_button_get_event(arg)));
    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_BOUND)                                                  // the initiator can only send control data to the responder when the devices are in bound status, which means the binding process has been completed successfully. Otherwise, the initiator will not send any control data to the responder and will log a message to remind the user to bind the devices firstly.
    {
        uint32_t bsr_espnow_data = (new_status ? 0x1 : 0x0) | (button_id & 0xF0000000);                    // the value sent to the responder is composed of the status of the button (pressed or not pressed) and the button ID (touche A in this case, which is represented by 0x1 in the most significant nibble of the value).
        ESP_LOGI(TAG, "initiator send press %c with value %08x (état %i)", button_name, bsr_espnow_data, new_status );
        espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, bsr_espnow_data);    // '1' pour "touche", ESPNOW_ATTRIBUTE_POWER pour le type d'attribut, 'bsr_espnow_data' pour la touche + valeur (0 ou 1)
        new_status = !new_status;                                                                               // toggle the status boolean for the next button press, so that the next value sent to the responder will be different from the previous one, which allows the responder to know if the button is pressed or released.
    } else {
        ESP_LOGI(TAG, "please double click to bind the devices firstly");
    }
    return new_status;
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button A is pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @return None
 ***********************************************************************************/
static void app_initiator_send_press_cb_A(void *arg, void *usr_data)
{
    static bool status = 0;         
    status = bsr_initiator_send_data(arg,status, 0x10000000, 'A');
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button Cis pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @return None
 ***********************************************************************************/
extern bool bsr_lvgl_lock(int timeout_ms);
extern void bsr_lvgl_unlock(void);

static void app_initiator_send_press_cb_C(void *arg, void *usr_data)
{
    /* Cycle drive mode: MANUEL → PRECISION → BALANCE → LOCK → MANUEL */
    bsr_drive_mode_t next = (bsr_data.mode + 1) % BSR_MODE_COUNT;
    ESP_LOGI(TAG, "Button C → mode change: %d -> %d", bsr_data.mode, next);

    if (bsr_lvgl_lock(-1)) {
        bsr_set_mode(next);
        bsr_lvgl_unlock();
    }
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button Bis pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @return None
 ***********************************************************************************/
// static void app_initiator_send_press_cb_B(void *arg, void *usr_data)
// {
//     static bool status = 0;
//     bsr_initiator_send_data(arg,status, 0x60000000, 'B');
// }

// Short press → back light (case 6)
static void app_initiator_send_press_cb_B_short(void *arg, void *usr_data)
{
    if (s_espnow_ctrl_status != APP_ESPNOW_CTRL_BOUND) {
        ESP_LOGI(TAG, "please bind devices first");
        return;
    }
    static bool status_rear = false;
    status_rear = !status_rear;
    uint32_t bsr_espnow_data = (status_rear ? 0x1 : 0x0) | (0x60000000); // case 6 = touche B
    ESP_LOGI(TAG, "B SHORT → Phares ARRIERE : %d", status_rear);
    espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, bsr_espnow_data);
}

// Long press → front light (case 7)
static void app_initiator_send_press_cb_B_long(void *arg, void *usr_data)
{
    if (s_espnow_ctrl_status != APP_ESPNOW_CTRL_BOUND) {
        ESP_LOGI(TAG, "please bind devices first");
        return;
    }
    static bool status_front = false;
    status_front = !status_front;
    uint32_t bsr_espnow_data = (status_front ? 0x1 : 0x0) | (0x70000000); // case 7 = phares avant
    ESP_LOGI(TAG, "B LONG → Phares AVANT : %d", status_front);
    espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, bsr_espnow_data);
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button F is pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @return None
 ***********************************************************************************/
static void app_initiator_send_press_cb_F(void *arg, void *usr_data)
{
    static bool status = 0;
    status = bsr_initiator_send_data(arg,status, 0x30000000, 'F');
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button 0 is pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @return None
 ***********************************************************************************/
static void app_initiator_send_press_cb_0(void *arg, void *usr_data)
{
    static bool status = 0;
    status = bsr_initiator_send_data(arg,status, 0x50000000, '0');
}

/***********************************************************************************
 * @brief Initiator callback to send control data to responder when button 1 is pressed.
 * @brief One callback is defined per button in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_initiator_send_press_cb_1(void *arg, void *usr_data)
{
    static bool status = 0;
    status = bsr_initiator_send_data(arg,status, 0x40000000, '1');
}

/***********************************************************************************
 * @brief Initiator callback to bind the initiator with responder when button A is double clicked, and unbind the initiator with responder when button A is long pressed.
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void bsr_do_bind_internal(void)
{
    ESP_LOGI(TAG, "initiator bind");
    espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
    s_espnow_ctrl_status = APP_ESPNOW_CTRL_BOUND;
    bsr_data.connected = true;
    bsr_data.rssi = 0;
    bsr_data.latency_ms = 0;
    bsr_data.pkt_loss_pct = 0.0f;
}

static void bsr_do_unbind_internal(void)
{
    ESP_LOGI(TAG, "initiator unbind");
    espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
    s_espnow_ctrl_status = APP_ESPNOW_CTRL_INIT;
    bsr_data.connected = false;
    bsr_data.rssi = 0;
    bsr_data.latency_ms = 0;
    bsr_data.pkt_loss_pct = 0.0f;
}

static void bsr_do_bind(void)
{
    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_INIT) {
        bsr_do_bind_internal();
        if (bsr_lvgl_lock(-1)) {
            bsr_update_connection();
            bsr_lvgl_unlock();
        }
    } else {
        ESP_LOGI(TAG, "this device is already in bound status");
    }
}

static void bsr_do_unbind(void)
{
    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_BOUND) {
        bsr_do_unbind_internal();
        if (bsr_lvgl_lock(-1)) {
            bsr_update_connection();
            bsr_lvgl_unlock();
        }
    } else {
        ESP_LOGI(TAG, "this device is not been bound");
    }
}

static void app_initiator_bind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == iot_button_get_event(arg)));
    if (bsr_lvgl_lock(-1)) {
        bsr_show_connection_page(5000);
        bsr_lvgl_unlock();
    }
    bsr_do_bind();
}

/***********************************************************************************
 * @brief Initiator callback to unbind the initiator with responder when button A is long pressed.
 *
 * @param[in] void *arg the button handle, which is set by user when registering the button callback.
 * @param[in] void *usr_data the user data, which is set by user when registering the button callback.
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_initiator_unbind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == iot_button_get_event(arg)));
    bsr_do_unbind();
}

/***********************************************************************************
 * @brief Initialize the button and register the button callbacks to send control data to responder when buttons are pressed.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_driver_init(void)
{
    esp_err_t err;

    /* Button 4.x API */
    button_config_t button_config =
    {
        .long_press_time = 2000,
        .short_press_time = 180,
    };

    button_gpio_config_t gpio_config_A =
    {
        .gpio_num = CONTROL_KEY_GPIO_A,
        .active_level = 0,
    };

    button_gpio_config_t gpio_config_B =
    {
        .gpio_num = CONTROL_KEY_GPIO_B,
        .active_level = 0,
    };

    button_gpio_config_t gpio_config_C =
    {
        .gpio_num = CONTROL_KEY_GPIO_C,
        .active_level = 0,
    };

    button_gpio_config_t gpio_config_F =
    {
        .gpio_num = CONTROL_KEY_GPIO_F,
        .active_level = 0,
    };

    button_gpio_config_t gpio_config_1 =
    {
        .gpio_num = CONTROL_KEY_GPIO_1,
        .active_level = 0,
    };

    button_gpio_config_t gpio_config_0 =
    {
        .gpio_num = CONTROL_KEY_GPIO_0,
        .active_level = 0,
    };


    // 6 buttons are implemented in this demo, but you can implement more buttons by using different bits of the "status" parameter in the callback functions.
    button_handle_t button_handle_1 = NULL;
    button_handle_t button_handle_F = NULL;
    button_handle_t button_handle_0 = NULL;
    button_handle_t button_handle_B = NULL;
    button_handle_t button_handle_A = NULL;
    button_handle_t button_handle_C = NULL;

    // Creates 6 button devices for 6 buttons in this demo, but you can create more button devices for more buttons by using different GPIOs and different button handles.
    ESP_LOGI(TAG, "Creates gpio device A");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_A, &button_handle_A);
    ESP_LOGI(TAG, "ret A = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Creates gpio device C");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_C, &button_handle_C);
    ESP_LOGI(TAG, "ret C = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Creates gpio device B");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_B, &button_handle_B);
    ESP_LOGI(TAG, "ret B = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Creates gpio device 0");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_0, &button_handle_0);
    ESP_LOGI(TAG, "ret 0 = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Creates gpio device F");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_F, &button_handle_F);
    ESP_LOGI(TAG, "ret F = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Creates gpio device 1");
    err = iot_button_new_gpio_device(&button_config, &gpio_config_1, &button_handle_1);
    ESP_LOGI(TAG, "ret 1 = %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Registers gpio callbacks");
    iot_button_register_cb(button_handle_A, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_A, NULL);
    iot_button_register_cb(button_handle_C, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_C, NULL);
    iot_button_register_cb(button_handle_B, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_B_short, NULL);
    iot_button_register_cb(button_handle_0, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_0, NULL);
    iot_button_register_cb(button_handle_1, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_1, NULL);
    iot_button_register_cb(button_handle_F, BUTTON_SINGLE_CLICK, NULL, app_initiator_send_press_cb_F, NULL);

    iot_button_register_cb(button_handle_A, BUTTON_DOUBLE_CLICK, NULL, app_initiator_bind_press_cb, NULL);          // Button A is used for bind and unbind in this demo, but you can use other buttons for bind and unbind, and you can also use other button events (such as long press, triple click, etc.) for bind and unbind.
    iot_button_register_cb(button_handle_A, BUTTON_LONG_PRESS_START, NULL, app_initiator_unbind_press_cb, NULL);    // Button A is used for bind and unbind in this demo, but you can use other buttons for bind and unbind, and you can also use other button events (such as single click, triple click, etc.) for bind and unbind.
    iot_button_register_cb(button_handle_B, BUTTON_LONG_PRESS_START, NULL, app_initiator_send_press_cb_B_long,  NULL); //Button B is used for activate/desactivate front light
}

/***********************************************************************************
 * @brief Responder callback to process control data received from initiator.
 * @brief The callback will be called when responder receives control data from initiator, and the control data is parsed by the library successfully.
 * @brief User can get the control data through the parameter "status", and then do some operations according to the control data.
 * @brief Only 6 buttons implemented in this demo, but you can implement more buttons by using different bits of the "status" parameter.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] espnow_attribute_t initiator_attribute the attribute of initiator, which is set by initiator when sending control data.
 * @param[in] espnow_attribute_t responder_attribute the attribute of responder, which is set by responder when sending control data.
 * @param[in] uint32_t status the control data sent by initiator, which is parsed by the library successfully.
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_responder_ctrl_data_cb(espnow_attribute_t initiator_attribute,
                              espnow_attribute_t responder_attribute,
                              uint32_t status)
{
    ESP_LOGI(TAG, "app_responder_ctrl_data_cb, initiator_attribute: %d, responder_attribute: %d, value: %08x",
             initiator_attribute, responder_attribute, status);

    bool pressed = false;
    switch ((status & 0xF0000000) >> 28)
    {
        case 1: // touche A
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 1 (A) pressed, with status : %d", pressed); break;
            break;
        case 2: // touche C
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 2 (C) pressed, with status : %d", pressed); break;
            break;
        case 3: // touche F
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 3 (F) pressed, with status : %d", pressed); break;
            break;
        case 4: // touche 1
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 4 (1) pressed, with status : %d", pressed); break;
            break;
        case 5: // touche 0
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 5 (0) pressed, with status : %d", pressed); break;
            break;
        case 6: // Back light (touche B short)
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Touch 6 (B) pressed, with status : %d", pressed); break;
            break;
        case 7: // |Front Light (touche B long)
            pressed = (status & 0xF) != 0;
            ESP_LOGI(TAG, "Phares AVANT : %d", pressed);
            // → gpio_set_level(PIN_PHARES_AVANT, pressed);
            break;
        case 8: 
        { // Settings packet (from screen)
            uint8_t  rx_mode     = (status >> 24) & 0x0F;
            uint8_t  rx_vmax     = (status >> 18) & 0x3F;
            uint8_t  rx_accel    = (status >> 15) & 0x07;
            uint8_t  rx_pmax     = (status >> 8)  & 0x7F;
            uint8_t  rx_dir_sens = (status >> 5)  & 0x07;
            uint8_t  rx_dir_dead = (status)       & 0x1F;
            ESP_LOGI(TAG, "RX Settings: mode=%d vmax=%d accel=%d pmax=%d dir_s=%d dir_d=%d",
                 rx_mode, rx_vmax, rx_accel, rx_pmax, rx_dir_sens, rx_dir_dead);
        // TODO: appliquer les réglages au robot ici
            break;
        }
        default:
            break;
        }
}

/***********************************************************************************
 * @brief Initialize the responder and register the control data callback to process control data received from initiator.
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_responder_init(void)
{
    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(30 * 1000, -55, NULL));
    espnow_ctrl_responder_data(app_responder_ctrl_data_cb);
}

/***********************************************************************************
 * @brief Responder callback to process control data received from initiator.
 *
 * @param[in] void *handler_args the user data, which is set by user when registering the event handler.
 * @param[in] esp_event_base_t base the event base, which is ESP_EVENT_ESPNOW in this case.
 * @param[in] int32_t id the event ID, which is ESP_EVENT_ESPNOW_CTRL_BIND, ESP_EVENT_ESPNOW_CTRL_UNBIND or ESP_EVENT_ESPNOW_CTRL_BIND_ERROR in this demo.
 * @param[in] void *event_data the event data, which is a pointer to the structure of the event data, and the structure is different for different event ID. For ESP_EVENT_ESPNOW_CTRL_BIND and ESP_EVENT_ESPNOW_CTRL_UNBIND, the event data is a pointer to espnow_ctrl_bind_info_t structure, which contains the information of the bind or unbind event. For ESP_EVENT_ESPNOW_CTRL_BIND_ERROR, the event data is a pointer to espnow_ctrl_bind_error_t structure, which contains the information of the bind error event.
 * @param[out] None
 * @return None
 ***********************************************************************************/
static void app_espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base != ESP_EVENT_ESPNOW)
    {
        return;
    }

    switch (id)
    {
        case ESP_EVENT_ESPNOW_CTRL_BIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "bind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            bsr_data.connected = true;
            if (bsr_lvgl_lock(-1)) {
                bsr_update_connection();
                bsr_lvgl_unlock();
            }
            break;
        }

        case ESP_EVENT_ESPNOW_CTRL_BIND_ERROR: {
            espnow_ctrl_bind_error_t *bind_error = (espnow_ctrl_bind_error_t *)event_data;
            ESP_LOGW(TAG, "bind error: %s", bind_error_to_string(*bind_error));
            bsr_data.connected = false;
            s_espnow_ctrl_status = APP_ESPNOW_CTRL_INIT;
            if (bsr_lvgl_lock(-1)) {
                bsr_update_connection();
                bsr_lvgl_unlock();
            }
            break;
        }

        case ESP_EVENT_ESPNOW_CTRL_UNBIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "unbind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            bsr_data.connected = false;
            if (bsr_lvgl_lock(-1)) {
                bsr_update_connection();
                bsr_lvgl_unlock();
            }
            break;
        }

        default:
            break;
    }
}

/***********************************************************************************
 * @brief Display the current Wi-Fi channel used by ESP‑NOW, which is useful for debugging and verifying that the devices are on the same channel for communication.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
void print_wifi_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_err_t err = esp_wifi_get_channel(&primary, &second);
    if (err == ESP_OK)
    {
        ESP_LOGI("WIFI", "Canal Wi-Fi/ESP-NOW actuel : %d", primary);
    } else
    {
        ESP_LOGE("WIFI", "esp_wifi_get_channel error: %s", esp_err_to_name(err));
    }
}

/***********************************************************************************
 * @brief Display the current Wi-Fi mode and PHY information, which is useful for debugging and verifying the Wi-Fi configuration of the device.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
void print_wifi_phy_info(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    ESP_LOGI("WIFI", "Mode Wi-Fi : %d", mode);

    uint8_t primary;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        ESP_LOGI("WIFI", "Canal : %d, second chan : %d", primary, second);
    }

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI("WIFI", "AP channel=%d, RSSI=%d, PHY: 11b=%d 11g=%d 11n=%d",
                 ap.primary, ap.rssi, ap.phy_11b, ap.phy_11g, ap.phy_11n);
    }
}

/**********************************************************************************/
/**********************************************************************************/

static void on_screen_bind_action(bool bind)
{
    /* Called from LVGL event context — mutex already held, don't re-lock */
    if (bind) {
        if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_INIT) {
            bsr_do_bind_internal();
            bsr_update_connection();
        } else {
            ESP_LOGI(TAG, "already bound");
        }
    } else {
        if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_BOUND) {
            bsr_do_unbind_internal();
            bsr_update_connection();
        } else {
            ESP_LOGI(TAG, "not bound");
        }
    }
}

void on_settings_changed(bsr_drive_mode_t mode, const bsr_data_t *data)
{
    if (s_espnow_ctrl_status != APP_ESPNOW_CTRL_BOUND) {
        ESP_LOGW(TAG, "Cannot send settings: not bound yet");
        return;
    }

    /*
     * Encode les réglages dans un uint32_t pour l'envoyer
     * via espnow_ctrl_initiator_send (ton système existant).
     *
     * Format du uint32_t :
     *   bits [31:28] = 0x8 → identifiant "paquet réglages" (case 8)
     *   bits [27:24] = mode (0-3)
     *   bits [23:18] = vmax_x10 (0-63, soit 0.0 à 6.3 km/h)
     *   bits [17:15] = accel (1-5, stocké sur 3 bits)
     *   bits [14:8]  = pmax (20-100, stocké sur 7 bits)
     *   bits [7:5]   = dir_sens (1-5)
     *   bits [4:0]   = dir_dead (0-30)
     */
    uint32_t pkt = 0x80000000;  /* case 8 = settings */
    pkt |= ((uint32_t)(mode & 0x0F))         << 24;
    pkt |= ((uint32_t)(data->mode_settings[mode].vmax_x10 & 0x3F)) << 18;
    pkt |= ((uint32_t)(data->mode_settings[mode].accel & 0x07))    << 15;
    pkt |= ((uint32_t)(data->mode_settings[mode].pmax & 0x7F))     << 8;
    pkt |= ((uint32_t)(data->mode_settings[mode].dir_sens & 0x07)) << 5;
    pkt |= ((uint32_t)(data->mode_settings[mode].dir_dead & 0x1F));

    ESP_LOGI(TAG, "Send settings: mode=%d vmax=%d accel=%d pmax=%d → 0x%08x",
             mode,
             data->mode_settings[mode].vmax_x10,
             data->mode_settings[mode].accel,
             data->mode_settings[mode].pmax,
             pkt);

    espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, pkt);
}

/***********************************************************************************
 * @brief Main application entry point, which initializes the Wi-Fi, driver, ESP‑NOW, and responder, and registers the event handler for ESP‑NOW events.
 * @brief 05/03/2024, by Laurent MARCHADIER - BEENETIC Systems SAS
 *
 * @param[in] none
 * @param[out] None
 * @return None
 ***********************************************************************************/
void bsr_espnow_init(void)
{
    espnow_storage_init();

    app_wifi_init();

    app_driver_init();

    // Configure ESP‑NOW with the default settings, which can be customized by modifying the fields of the espnow_config structure before calling espnow_init.
    // The espnow_config structure contains various configuration parameters for ESP‑NOW, such as forwarding behavior, security settings, queue size, retry numbers, timeouts, and receive filters.
    // You can modify these parameters according to your application needs before initializing ESP‑NOW.
    // For example, to enable forwarding and allow channel switching for forwarding, you can set the following fields of espnow_config:
    //      espnow_config.forward_enable = 1;
    //      espnow_config.forward_switch_channel = 1;
    // To enable security and set a PMK, you can set the following fields of espnow_config:
    //      espnow_config.sec_enable = 1;
    //      espnow_config.pmk = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();

    // For this demo, we use the default configuration of ESP‑NOW, which is suitable for a simple point‑to‑point communication scenario without forwarding or security. You can modify the configuration parameters as needed for your application.
    espnow_init(&espnow_config);

    // Register the event handler for ESP‑NOW events, which will be called when certain events related to ESP‑NOW occur, such as bind, unbind, bind error, etc. The event handler can be used to process these events and perform actions accordingly.
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, app_espnow_event_handler, NULL);

    // Initialize the responder, which will set up the responder role for ESP‑NOW and register the callback to process control data received from the initiator. The responder will be able to receive control data sent by the initiator and execute the corresponding actions based on the received data.
    app_responder_init();

    bsr_register_settings_changed_cb(on_settings_changed);
    bsr_register_bind_action_cb(on_screen_bind_action);

    // Print the current Wi-Fi channel and PHY information for debugging purposes, which can help you verify that the devices are on the same channel and have the correct Wi-Fi configuration for ESP‑NOW communication.
    print_wifi_channel();

    // Print the current Wi-Fi PHY information for debugging purposes.
    print_wifi_phy_info();

    ESP_LOGI(TAG, "=== BSR Telecommande ready ===");
}