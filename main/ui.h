#pragma once
#include "ha_client.h"
#include "wifi_manager.h"
#include <stdint.h>

typedef void (*ui_lock_action_cb_t)(bool lock);      /* true=lock, false=unlock */
typedef void (*ui_vent_action_cb_t)(uint8_t speed);  /* 2/3/4 */

void ui_init(ui_lock_action_cb_t lock_cb, ui_vent_action_cb_t vent_cb);

void ui_set_wifi_state(wifi_state_t state);
void ui_set_mqtt_state(bool connected);
void ui_set_lock_state(lock_state_t state);
void ui_set_vent_speed(uint8_t speed);
void ui_show_feedback(const char *msg);
