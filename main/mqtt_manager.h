#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*mqtt_state_cb_t)(bool connected);
typedef void (*mqtt_vent_speed_cb_t)(uint8_t speed);

void mqtt_manager_init(const char *host, uint16_t port,
                       const char *user, const char *pass,
                       mqtt_state_cb_t state_cb,
                       mqtt_vent_speed_cb_t vent_speed_cb);
bool mqtt_manager_publish_vent_speed(uint8_t speed);
bool mqtt_manager_is_connected(void);
