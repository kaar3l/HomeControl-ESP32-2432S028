#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#define TAG             "wifi"
#define AP_SSID         "HomeControl-Setup"
#define AP_PASSWORD     "homecontrol"
#define CONNECT_TIMEOUT_MS  15000
#define MAX_RETRY       3

static wifi_state_t     s_state = WIFI_STATE_DISCONNECTED;
static wifi_state_cb_t  s_cb;
static int              s_retry;
static char             s_ap_ip[16] = "192.168.4.1";

static void set_state(wifi_state_t state)
{
    s_state = state;
    if (s_cb) s_cb(state);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting");
            set_state(WIFI_STATE_CONNECTING);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_state == WIFI_STATE_CONNECTED) {
                ESP_LOGW(TAG, "disconnected, retrying");
                set_state(WIFI_STATE_DISCONNECTED);
                /* Reset so temporary drops don't consume the AP-fallback budget */
                s_retry = 0;
            }
            s_retry++;
            if (s_retry <= MAX_RETRY) {
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "max retries reached, switching to AP");
                wifi_manager_start_ap();
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "client connected to AP");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        set_state(WIFI_STATE_CONNECTED);
    }
}

void wifi_manager_init(const char *ssid, const char *password, wifi_state_cb_t cb)
{
    s_cb = cb;
    s_retry = 0;

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no SSID configured, starting AP");
        wifi_manager_start_ap();
        return;
    }

    wifi_config_t wcfg = {0};
    strlcpy((char *)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_start();
}

void wifi_manager_start_ap(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;

    wifi_config_t wcfg = {0};
    strlcpy((char *)wcfg.ap.ssid,     AP_SSID,     sizeof(wcfg.ap.ssid));
    strlcpy((char *)wcfg.ap.password, AP_PASSWORD, sizeof(wcfg.ap.password));
    wcfg.ap.ssid_len    = strlen(AP_SSID);
    wcfg.ap.authmode    = WIFI_AUTH_WPA2_PSK;
    wcfg.ap.max_connection = 4;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    esp_wifi_start();

    set_state(WIFI_STATE_AP_MODE);
    ESP_LOGI(TAG, "AP started: SSID=%s password=%s", AP_SSID, AP_PASSWORD);
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

const char *wifi_manager_get_ap_ip(void)
{
    return s_ap_ip;
}
