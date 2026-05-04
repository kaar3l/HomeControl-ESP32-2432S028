#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"

#define TAG "settings"
#define NS  "homecontrol"

/* NVS version tag for 4-point affine calibration format */
#define TOUCH_CAL_NVS_VERSION 2

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing");
        nvs_flash_erase();
        nvs_flash_init();
    }
}

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK)
        dst[0] = '\0';
}

void settings_load(app_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->mqtt_port = 1883;
    s->last_vent_speed = 3;
    /* Identity defaults (raw == screen pixel) — wizard runs on first boot */
    s->touch_cal_rx[0] =  40; s->touch_cal_ry[0] =  40;
    s->touch_cal_rx[1] = 279; s->touch_cal_ry[1] =  40;
    s->touch_cal_rx[2] = 279; s->touch_cal_ry[2] = 199;
    s->touch_cal_rx[3] =  40; s->touch_cal_ry[3] = 199;
    s->touch_calibrated = 0;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;

    load_str(h, "wifi_ssid", s->wifi_ssid,     sizeof(s->wifi_ssid));
    load_str(h, "wifi_pass", s->wifi_password, sizeof(s->wifi_password));
    load_str(h, "mqtt_host", s->mqtt_host,      sizeof(s->mqtt_host));
    load_str(h, "mqtt_user", s->mqtt_user,      sizeof(s->mqtt_user));
    load_str(h, "mqtt_pass", s->mqtt_pass,      sizeof(s->mqtt_pass));
    load_str(h, "ha_url",    s->ha_url,         sizeof(s->ha_url));
    load_str(h, "ha_token",  s->ha_token,       sizeof(s->ha_token));

    uint16_t port = 1883;
    if (nvs_get_u16(h, "mqtt_port", &port) == ESP_OK) s->mqtt_port = port;

    uint8_t spd = 3;
    if (nvs_get_u8(h, "vent_speed", &spd) == ESP_OK) s->last_vent_speed = spd;

    /* Touch calibration — only load if stored with the current format version */
    uint8_t calver = 0;
    if (nvs_get_u8(h, "touch_calid", &calver) == ESP_OK
        && calver == TOUCH_CAL_NVS_VERSION) {
        bool ok = true;
        for (int i = 0; i < 4 && ok; i++) {
            char key[10];
            uint16_t v;
            snprintf(key, sizeof(key), "tcal_%dx", i);
            ok = ok && (nvs_get_u16(h, key, &v) == ESP_OK);
            if (ok) s->touch_cal_rx[i] = v;
            snprintf(key, sizeof(key), "tcal_%dy", i);
            ok = ok && (nvs_get_u16(h, key, &v) == ESP_OK);
            if (ok) s->touch_cal_ry[i] = v;
        }
        if (ok) s->touch_calibrated = 1;
    }

    nvs_close(h);
}

void settings_save(const app_settings_t *s)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_str(h, "wifi_ssid", s->wifi_ssid);
    nvs_set_str(h, "wifi_pass", s->wifi_password);
    nvs_set_str(h, "mqtt_host", s->mqtt_host);
    nvs_set_str(h, "mqtt_user", s->mqtt_user);
    nvs_set_str(h, "mqtt_pass", s->mqtt_pass);
    nvs_set_str(h, "ha_url",    s->ha_url);
    nvs_set_str(h, "ha_token",  s->ha_token);
    nvs_set_u16(h, "mqtt_port", s->mqtt_port);
    nvs_set_u8 (h, "vent_speed", s->last_vent_speed);

    for (int i = 0; i < 4; i++) {
        char key[10];
        snprintf(key, sizeof(key), "tcal_%dx", i);
        nvs_set_u16(h, key, s->touch_cal_rx[i]);
        snprintf(key, sizeof(key), "tcal_%dy", i);
        nvs_set_u16(h, key, s->touch_cal_ry[i]);
    }
    nvs_set_u8(h, "touch_calid",
               s->touch_calibrated ? TOUCH_CAL_NVS_VERSION : 0);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "settings saved");
}

void settings_save_vent_speed(uint8_t speed)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "vent_speed", speed);
    nvs_commit(h);
    nvs_close(h);
}
