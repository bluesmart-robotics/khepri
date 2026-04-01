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
 * @file bsr_espnow_control.h
 * @brief ESP-NOW remote control demo for BSR, which demonstrates how to use ESP-NOW to implement a simple remote control application, where one device (initiator) can send control data to another device (responder) when buttons are pressed, and the responder can process the received control data and execute corresponding actions.
 * @author Laurent MARCHADIER - BEENETIC Systems SAS
 * @contact: laurent.marchadier@beenetic.fr
 * @version 1.0
 * ________________________________________________________________________
*/

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow.h"
#include "espnow_ctrl.h"
#include "espnow_utils.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/rmt.h"
#endif
#include "driver/gpio.h"
#include "led_strip.h"

#include "iot_button.h"
#include "button_gpio.h"
#include "esp_err.h"


// GPIO definitions for the buttons used in this demo, which can be modified according to the actual hardware design and button connections.
// Each button is assigned a unique GPIO number, which is used to initialize the button and register the corresponding callback for sending control data to the responder when the button is pressed.
#define CONTROL_KEY_GPIO_A      GPIO_NUM_15
#define CONTROL_KEY_GPIO_C      GPIO_NUM_17
#define CONTROL_KEY_GPIO_B      GPIO_NUM_8
#define CONTROL_KEY_GPIO_0      GPIO_NUM_NC  /* pas de GPIO attribue */
#define CONTROL_KEY_GPIO_1      GPIO_NUM_NC  /* pas de GPIO attribue */
#define CONTROL_KEY_GPIO_F      GPIO_NUM_7

void bsr_espnow_init(void);