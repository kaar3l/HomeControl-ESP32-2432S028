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
    uint16_t touch_cal_rx[4]; /* raw XPT2046 X at 4 cal points (TL,TR,BR,BL) — must match TOUCH_CAL_COUNT */
    uint16_t touch_cal_ry[4]; /* raw XPT2046 Y at 4 cal points */
    uint8_t  touch_calibrated; /* 1 = valid 4-point calibration stored (NVS version 2) */
} app_settings_t;

void settings_init(void);
void settings_load(app_settings_t *s);
void settings_save(const app_settings_t *s);
void settings_save_vent_speed(uint8_t speed);
