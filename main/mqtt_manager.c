#include "mqtt_manager.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG          "mqtt"
#define VENT_TOPIC   "/home/vent/speed"

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static mqtt_state_cb_t s_state_cb;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch (ev->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        s_connected = true;
        if (s_state_cb) s_state_cb(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        if (s_state_cb) s_state_cb(false);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "error");
        break;
    default:
        break;
    }
}

void mqtt_manager_init(const char *host, uint16_t port,
                       const char *user, const char *pass,
                       mqtt_state_cb_t state_cb)
{
    s_state_cb = state_cb;

    if (!host || host[0] == '\0') {
        ESP_LOGW(TAG, "no MQTT host configured");
        return;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", host, port);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
    };
    if (user && user[0]) {
        cfg.credentials.username = user;
        cfg.credentials.authentication.password = pass;
    }

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "starting: %s", uri);
}

bool mqtt_manager_publish_vent_speed(uint8_t speed)
{
    if (!s_connected || !s_client) return false;

    char payload[4];
    snprintf(payload, sizeof(payload), "%u", speed);
    int msg_id = esp_mqtt_client_publish(s_client, VENT_TOPIC, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed");
        return false;
    }
    ESP_LOGI(TAG, "published %s -> %s", VENT_TOPIC, payload);
    return true;
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}
