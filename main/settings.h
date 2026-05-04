#pragma once
#include <stdint.h>

typedef struct {
    char wifi_ssid[64];
    char wifi_password[64];
    char mqtt_host[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char ha_url[128];     /* e.g. http://192.168.1.100:8123 */
    char ha_token[320];   /* long-lived access token */
    uint8_t last_vent_speed; /* 2=Low 3=Normal 4=Fast */
} app_settings_t;

void settings_init(void);
void settings_load(app_settings_t *s);
void settings_save(const app_settings_t *s);
void settings_save_vent_speed(uint8_t speed);
