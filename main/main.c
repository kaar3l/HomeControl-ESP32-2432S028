#include "settings.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "mqtt_manager.h"
#include "ha_client.h"
#include "display.h"
#include "ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_event.h"

#define TAG                  "main"
#define HA_POLL_INTERVAL_MS  8000

static app_settings_t  s_settings;
static bool            s_services_started;

/* 1-slot queue: LVGL task writes lock commands, main task executes them.
 * Keeps all HTTP off the LVGL task so rendering never blocks on network I/O. */
static QueueHandle_t   s_lock_cmd_queue;

/* ------------------------------------------------------------------ */
/* UI callbacks — called from LVGL task, must not do blocking I/O     */
/* ------------------------------------------------------------------ */
static void on_lock_action(bool lock)
{
    ESP_LOGI(TAG, "lock action queued: %s", lock ? "lock" : "unlock");
    ui_show_feedback(lock ? "Locking..." : "Unlocking...");
    xQueueOverwrite(s_lock_cmd_queue, &lock);
}

static void on_vent_action(uint8_t speed)
{
    ESP_LOGI(TAG, "vent speed: %u", speed);
    s_settings.last_vent_speed = speed;
    settings_save_vent_speed(speed);
    ui_set_vent_speed(speed);

    if (!mqtt_manager_publish_vent_speed(speed))
        ui_show_feedback("MQTT publish failed");
    else
        ui_show_feedback("Ventilation updated");
}

/* ------------------------------------------------------------------ */
/* HA lock state callback                                              */
/* ------------------------------------------------------------------ */
static void on_lock_state(lock_state_t state)
{
    ui_set_lock_state(state);
}

/* ------------------------------------------------------------------ */
/* MQTT state callback                                                 */
/* ------------------------------------------------------------------ */
static void on_mqtt_state(bool connected)
{
    ui_set_mqtt_state(connected);
}

/* ------------------------------------------------------------------ */
/* WiFi state callback                                                 */
/* ------------------------------------------------------------------ */
static void on_wifi_state(wifi_state_t state)
{
    ui_set_wifi_state(state);

    if (state == WIFI_STATE_CONNECTED && !s_services_started) {
        s_services_started = true;
        ESP_LOGI(TAG, "WiFi up, starting MQTT and HA client");
        mqtt_manager_init(s_settings.mqtt_host, s_settings.mqtt_port,
                          s_settings.mqtt_user, s_settings.mqtt_pass,
                          on_mqtt_state);
        ha_client_init(s_settings.ha_url, s_settings.ha_token, on_lock_state);
    }
}

/* ------------------------------------------------------------------ */
/* Settings saved callback (from web server)                           */
/* ------------------------------------------------------------------ */
static void on_settings_saved(const app_settings_t *new_settings)
{
    /* web_server calls esp_restart() before returning, so this is unreachable */
    (void)new_settings;
}

/* ------------------------------------------------------------------ */
/* Main entry point                                                    */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    /* 1. System init */
    esp_netif_init();
    esp_event_loop_create_default();

    /* 2. Persistent storage */
    settings_init();
    settings_load(&s_settings);
    ESP_LOGI(TAG, "settings loaded, SSID='%s' HA='%s'",
             s_settings.wifi_ssid, s_settings.ha_url);

    /* 3. Lock-action command queue (capacity 1 — latest command wins) */
    s_lock_cmd_queue = xQueueCreate(1, sizeof(bool));

    /* 4. Display + LVGL */
    display_init();
    ui_init(on_lock_action, on_vent_action);

    ui_set_vent_speed(s_settings.last_vent_speed);
    ui_set_lock_state(LOCK_STATE_UNKNOWN);

    /* 5. Web server (settings + OTA) — always available */
    web_server_start(&s_settings, on_settings_saved);

    /* 6. WiFi (STA or AP fallback) */
    wifi_manager_init(s_settings.wifi_ssid, s_settings.wifi_password, on_wifi_state);

    /* 7. Main loop
     *    - Blocks up to HA_POLL_INTERVAL_MS waiting for a lock command.
     *    - If a command arrives it is executed immediately; state is then
     *      refreshed with an extra poll so the UI updates without waiting
     *      for the next regular cycle.
     *    - After each command (or timeout) the regular poll fires. */
    while (1) {
        bool do_lock;
        bool got_cmd = (xQueueReceive(s_lock_cmd_queue, &do_lock,
                                      pdMS_TO_TICKS(HA_POLL_INTERVAL_MS)) == pdTRUE);

        if (got_cmd) {
            if (wifi_manager_get_state() == WIFI_STATE_CONNECTED) {
                bool ok = do_lock ? ha_client_lock() : ha_client_unlock();
                if (!ok)
                    ui_show_feedback("Action failed — check HA connection");
                else
                    ha_client_poll(); /* refresh state immediately after action */
            } else {
                ui_show_feedback("Not connected to Wi-Fi");
            }
        }

        if (wifi_manager_get_state() == WIFI_STATE_CONNECTED) {
            ha_client_poll();
        }
    }
}
