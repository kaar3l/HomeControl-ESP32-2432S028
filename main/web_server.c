#include "web_server.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define WS_MIN(a, b) ((a) < (b) ? (a) : (b))

#define TAG "web_server"

static httpd_handle_t    s_server;
static app_settings_t   *s_settings;
static settings_saved_cb_t s_cb;

/* ------------------------------------------------------------------ */
/* Settings HTML page                                                  */
/* ------------------------------------------------------------------ */
static const char SETTINGS_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>HomeControl Setup</title>"
"<style>"
"body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}"
"h1{font-size:1.4em}label{display:block;margin-top:1em;font-weight:bold}"
"input{width:100%%;padding:.4em;box-sizing:border-box;margin-top:.3em}"
"button{margin-top:1.5em;padding:.6em 1.4em;font-size:1em;cursor:pointer}"
"hr{margin:2em 0}.note{font-size:.85em;color:#555}"
"</style></head><body>"
"<h1>HomeControl Setup</h1>"
"<form method='POST' action='/save'>"
"<h2>Wi-Fi</h2>"
"<label>SSID<input name='wifi_ssid' value='%s'></label>"
"<label>Password<input type='password' name='wifi_pass' value='%s'></label>"
"<h2>Home Assistant</h2>"
"<label>URL (e.g. http://192.168.1.100:8123)<input name='ha_url' value='%s'></label>"
"<label>Long-Lived Access Token<input name='ha_token' value='%s'></label>"
"<h2>MQTT</h2>"
"<label>Host<input name='mqtt_host' value='%s'></label>"
"<label>Port<input name='mqtt_port' type='number' value='%u'></label>"
"<label>Username (optional)<input name='mqtt_user' value='%s'></label>"
"<label>Password (optional)<input type='password' name='mqtt_pass' value='%s'></label>"
"<button type='submit'>Save &amp; Restart</button>"
"</form>"
"<hr>"
"<h2>Firmware Update (OTA)</h2>"
"<form method='POST' action='/ota' enctype='multipart/form-data'>"
"<input type='file' name='firmware' accept='.bin'>"
"<button type='submit'>Upload Firmware</button>"
"</form>"
"<p class='note'>After uploading, the device will reboot into the new firmware.</p>"
"<hr>"
"<h2>Touch Calibration</h2>"
"<form method='POST' action='/calibrate'>"
"<p class='note'>Clears saved calibration. The device will restart and show the calibration wizard.</p>"
"<button type='submit'>Recalibrate Touch</button>"
"</form>"
"</body></html>";

static esp_err_t handle_root(httpd_req_t *req)
{
    char *buf = malloc(sizeof(SETTINGS_HTML) + 1600);
    if (!buf) return ESP_ERR_NO_MEM;
    snprintf(buf, sizeof(SETTINGS_HTML) + 1024, SETTINGS_HTML,
             s_settings->wifi_ssid,
             s_settings->wifi_password,
             s_settings->ha_url,
             s_settings->ha_token,
             s_settings->mqtt_host,
             s_settings->mqtt_port,
             s_settings->mqtt_user,
             s_settings->mqtt_pass);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}

/* Parse "key=value&key2=value2" form body */
static void parse_field(const char *body, const char *key, char *dst, size_t dst_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { dst[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, p, len);
    dst[len] = '\0';

    /* URL-decode '+' as space */
    for (size_t i = 0; i < len; i++)
        if (dst[i] == '+') dst[i] = ' ';

    /* Basic %XX decode */
    char *out = dst;
    char *in  = dst;
    while (*in) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], 0};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
}

static esp_err_t handle_save(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) return ESP_ERR_NO_MEM;

    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); return ESP_FAIL; }
    body[received] = '\0';

    parse_field(body, "wifi_ssid",  s_settings->wifi_ssid,     sizeof(s_settings->wifi_ssid));
    parse_field(body, "wifi_pass",  s_settings->wifi_password, sizeof(s_settings->wifi_password));
    parse_field(body, "ha_url",     s_settings->ha_url,        sizeof(s_settings->ha_url));
    parse_field(body, "ha_token",   s_settings->ha_token,      sizeof(s_settings->ha_token));
    parse_field(body, "mqtt_host",  s_settings->mqtt_host,     sizeof(s_settings->mqtt_host));
    parse_field(body, "mqtt_user",  s_settings->mqtt_user,     sizeof(s_settings->mqtt_user));
    parse_field(body, "mqtt_pass",  s_settings->mqtt_pass,     sizeof(s_settings->mqtt_pass));

    char port_str[8] = "1883";
    parse_field(body, "mqtt_port", port_str, sizeof(port_str));
    s_settings->mqtt_port = (uint16_t)atoi(port_str);

    free(body);

    settings_save(s_settings);
    if (s_cb) s_cb(s_settings);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body>"
        "<h2>Settings saved. Rebooting...</h2>"
        "<script>setTimeout(()=>location.href='/',5000)</script>"
        "</body></html>");

    /* Reboot after response is sent */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* OTA upload handler                                                  */
/* ------------------------------------------------------------------ */
static esp_err_t handle_ota(httpd_req_t *req)
{
#define OTA_BUF_SIZE 1024
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA partition: %s", update_partition->label);

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) return ESP_ERR_NO_MEM;

    int remaining = req->content_len;
    bool started  = false;

    /* Skip multipart header: find \r\n\r\n */
    /* For simplicity we accept raw binary or skip the multipart preamble */
    /* Read first chunk to find the binary start */
    int first = httpd_req_recv(req, buf, OTA_BUF_SIZE);
    if (first <= 0) { free(buf); return ESP_FAIL; }
    remaining -= first;

    /* Detect multipart: starts with "--" */
    char *data_start = buf;
    int   data_len   = first;
    if (buf[0] == '-' && buf[1] == '-') {
        /* Find the double CRLF that ends the part headers */
        char *sep = memmem(buf, first, "\r\n\r\n", 4);
        if (sep) {
            data_start = sep + 4;
            data_len   = first - (int)(data_start - buf);
        }
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
        return err;
    }
    started = true;

    if (data_len > 0) esp_ota_write(ota_handle, data_start, data_len);

    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, WS_MIN(OTA_BUF_SIZE, remaining));
        if (n <= 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) break;
        remaining -= n;
    }
    free(buf);

    if (err != ESP_OK || !started) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota end failed");
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot failed");
        return err;
    }

    ESP_LOGI(TAG, "OTA success, rebooting");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body>"
        "<h2>Firmware updated. Rebooting...</h2>"
        "</body></html>");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Calibration reset handler                                           */
/* ------------------------------------------------------------------ */
static esp_err_t handle_calibrate(httpd_req_t *req)
{
    s_settings->touch_calibrated = 0;
    settings_save(s_settings);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body>"
        "<h2>Calibration cleared. Rebooting...</h2>"
        "<script>setTimeout(()=>location.href='/',8000)</script>"
        "</body></html>");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void web_server_start(app_settings_t *settings, settings_saved_cb_t cb)
{
    s_settings = settings;
    s_cb = cb;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start httpd");
        return;
    }

    httpd_uri_t uri_root = { .uri = "/",           .method = HTTP_GET,  .handler = handle_root      };
    httpd_uri_t uri_save = { .uri = "/save",       .method = HTTP_POST, .handler = handle_save      };
    httpd_uri_t uri_ota  = { .uri = "/ota",        .method = HTTP_POST, .handler = handle_ota       };
    httpd_uri_t uri_cal  = { .uri = "/calibrate",  .method = HTTP_POST, .handler = handle_calibrate };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_save);
    httpd_register_uri_handler(s_server, &uri_ota);
    httpd_register_uri_handler(s_server, &uri_cal);

    ESP_LOGI(TAG, "started on port %d", cfg.server_port);
}

void web_server_stop(void)
{
    if (s_server) httpd_stop(s_server);
}
