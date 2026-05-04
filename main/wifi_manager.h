#pragma once
#include <stdbool.h>

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
} wifi_state_t;

typedef void (*wifi_state_cb_t)(wifi_state_t state);

void wifi_manager_init(const char *ssid, const char *password, wifi_state_cb_t cb);
wifi_state_t wifi_manager_get_state(void);
void wifi_manager_start_ap(void);
const char *wifi_manager_get_ap_ip(void);
