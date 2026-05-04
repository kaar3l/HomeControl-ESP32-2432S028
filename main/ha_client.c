#include "ha_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define TAG              "ha_client"
#define LOCK_ENTITY      "lock.front_door"
#define RESPONSE_BUF_LEN 4096

static char           s_base_url[128];
static char           s_token[320];
static lock_state_cb_t s_cb;
static lock_state_t    s_state = LOCK_STATE_UNKNOWN;
static char            s_resp_buf[RESPONSE_BUF_LEN];
static int             s_resp_len;

static esp_err_t http_event_handler(esp_http_client_event_t *ev)
{
    switch (ev->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_resp_len + ev->data_len < RESPONSE_BUF_LEN - 1) {
            memcpy(s_resp_buf + s_resp_len, ev->data, ev->data_len);
            s_resp_len += ev->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        s_resp_buf[s_resp_len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

void ha_client_init(const char *base_url, const char *token, lock_state_cb_t cb)
{
    /* Prepend http:// if the user omitted the scheme */
    if (strncmp(base_url, "http://", 7) != 0 &&
        strncmp(base_url, "https://", 8) != 0) {
        snprintf(s_base_url, sizeof(s_base_url), "http://%s", base_url);
    } else {
        strlcpy(s_base_url, base_url, sizeof(s_base_url));
    }
    /* Strip trailing slash so path concatenation is always clean */
    size_t len = strlen(s_base_url);
    if (len > 0 && s_base_url[len - 1] == '/') s_base_url[len - 1] = '\0';

    strlcpy(s_token, token, sizeof(s_token));
    s_cb = cb;
}

static bool do_request(const char *method, const char *path,
                       const char *body, int *status_out)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    char auth[340];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);

    s_resp_len = 0;
    s_resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url            = url,
        .event_handler  = http_event_handler,
        .timeout_ms     = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_method(client, strcmp(method, "POST") == 0
                                       ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    if (body) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    if (status_out) *status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return err == ESP_OK;
}

void ha_client_poll(void)
{
    if (s_base_url[0] == '\0' || s_token[0] == '\0') return;

    char path[64];
    snprintf(path, sizeof(path), "/api/states/%s", LOCK_ENTITY);

    int status = 0;
    if (!do_request("GET", path, NULL, &status) || status != 200) {
        ESP_LOGW(TAG, "poll failed, status=%d", status);
        return;
    }

    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) return;

    lock_state_t new_state = LOCK_STATE_UNKNOWN;
    cJSON *state_node = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(state_node)) {
        if (strcmp(state_node->valuestring, "locked") == 0)
            new_state = LOCK_STATE_LOCKED;
        else if (strcmp(state_node->valuestring, "unlocked") == 0)
            new_state = LOCK_STATE_UNLOCKED;
    }
    cJSON_Delete(root);

    if (new_state != s_state) {
        s_state = new_state;
        ESP_LOGI(TAG, "lock state: %s",
                 new_state == LOCK_STATE_LOCKED ? "locked" :
                 new_state == LOCK_STATE_UNLOCKED ? "unlocked" : "unknown");
        if (s_cb) s_cb(s_state);
    }
}

static bool call_lock_service(const char *service)
{
    char path[64];
    snprintf(path, sizeof(path), "/api/services/lock/%s", service);
    char body[64];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", LOCK_ENTITY);

    int status = 0;
    bool ok = do_request("POST", path, body, &status);
    ESP_LOGI(TAG, "service lock/%s status=%d", service, status);
    return ok && status == 200;
}

bool ha_client_lock(void)   { return call_lock_service("lock"); }
bool ha_client_unlock(void) { return call_lock_service("unlock"); }

lock_state_t ha_client_get_state(void) { return s_state; }
