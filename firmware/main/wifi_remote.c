#include "wifi_remote.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detection_cache.h"
#include "ble_scanner.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "btrpa_wifi";

static esp_err_t json_bad_request(httpd_req_t *req, const char *field, const char *message)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    char out[160];
    snprintf(out, sizeof(out), "{\"error\":\"invalid_%s\",\"message\":\"%s\"}", field, message);
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

static bool is_hex_digit_char(char c)
{
    return isxdigit((unsigned char)c) != 0;
}

static bool valid_mac_address(const char *s)
{
    if (s == NULL || strlen(s) != 17) return false;
    for (int i = 0; i < 17; ++i) {
        if ((i + 1) % 3 == 0) {
            if (s[i] != ':') return false;
        } else if (!is_hex_digit_char(s[i])) {
            return false;
        }
    }
    return true;
}

static bool parse_float_range(const char *s, float min, float max, float *out)
{
    if (s == NULL || *s == '\0') return false;
    errno = 0;
    char *end = NULL;
    float value = strtof(s, &end);
    if (errno != 0 || end == s || *end != '\0' || !isfinite(value) || value < min || value > max) return false;
    *out = value;
    return true;
}

static bool parse_int_range(const char *s, int min, int max, int *out)
{
    if (s == NULL || *s == '\0') return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < min || value > max) return false;
    *out = (int)value;
    return true;
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/x-ndjson");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    btrpa_detection_t items[BTRPA_DETECTION_CACHE_SIZE];
    size_t n = btrpa_detection_cache_snapshot(items, BTRPA_DETECTION_CACHE_SIZE);
    char line[BTRPA_DETECTION_JSON_MAX];
    for (size_t i = 0; i < n; ++i) {
        btrpa_detection_to_json(&items[i], line, sizeof(line));
        httpd_resp_sendstr_chunk(req, line);
        httpd_resp_sendstr_chunk(req, "\n");
    }
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char track[384]; btrpa_track_json(track, sizeof(track));
    char out[640]; snprintf(out, sizeof(out), "{\"device\":\"ESP32-BTRPA-Tracker\",\"mode\":\"%s\",\"scan\":\"%s\",\"gps\":\"absent\",\"gps_source\":\"phone_fallback_supported\",\"endpoint\":\"/scan\",\"track\":%s}", btrpa_ble_mode_name(btrpa_ble_get_mode()), btrpa_ble_get_mode()==BTRPA_BLE_MODE_GATT?"paused_for_gatt":"running", track);
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

static void query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char q[256] = {0}; out[0] = '\0';
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, key, out, out_len);
}

static esp_err_t mode_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (req->method == HTTP_POST || req->method == HTTP_GET) {
        char value[24]; query_value(req, "value", value, sizeof(value));
        if (value[0]) { btrpa_ble_mode_t m; if (!btrpa_ble_parse_mode(value, &m)) return json_bad_request(req, "mode", "mode must be scanner, gatt, or hybrid"); btrpa_ble_set_mode(m); }
    }
    char out[96]; snprintf(out, sizeof(out), "{\"mode\":\"%s\"}", btrpa_ble_mode_name(btrpa_ble_get_mode())); httpd_resp_sendstr(req, out); return ESP_OK;
}

static esp_err_t track_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char off[8]; query_value(req, "off", off, sizeof(off));
    if (off[0] == '1') btrpa_track_off();
    else {
        btrpa_track_config_t cfg; btrpa_track_get(&cfg);
        char v[80]; query_value(req, "address", v, sizeof(v)); if (v[0]) { if (!valid_mac_address(v)) return json_bad_request(req, "address", "address must use AA:BB:CC:DD:EE:FF format"); strlcpy(cfg.address, v, sizeof(cfg.address)); }
        query_value(req, "name", v, sizeof(v)); if (v[0]) strlcpy(cfg.name, v, sizeof(cfg.name));
        query_value(req, "meters", v, sizeof(v)); if (v[0]) { float meters; if (!parse_float_range(v, 0.3f, 80.0f, &meters)) return json_bad_request(req, "meters", "meters must be a number from 0.3 to 80.0"); cfg.meters = meters; }
        query_value(req, "rssi", v, sizeof(v)); if (v[0]) { int rssi; if (!parse_int_range(v, -127, 20, &rssi)) return json_bad_request(req, "rssi", "rssi must be an integer from -127 to 20"); cfg.min_rssi = rssi; cfg.use_rssi = true; }
        if (cfg.address[0] || cfg.name[0]) btrpa_track_set(&cfg);
    }
    char out[384]; btrpa_track_json(out, sizeof(out)); httpd_resp_sendstr(req, out); return ESP_OK;
}

esp_err_t btrpa_wifi_remote_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, BTRPA_WIFI_AP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, BTRPA_WIFI_AP_PASS, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(BTRPA_WIFI_AP_SSID);
    ap.ap.channel = BTRPA_WIFI_AP_CHANNEL;
    ap.ap.max_connection = BTRPA_WIFI_AP_MAX_STA;
    ap.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(BTRPA_WIFI_AP_PASS) == 0) {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &http_cfg));
    httpd_uri_t scan_uri = {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler};
    httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status_handler};
    httpd_uri_t mode_uri = {.uri = "/mode", .method = HTTP_GET, .handler = mode_handler};
    httpd_uri_t mode_post_uri = {.uri = "/mode", .method = HTTP_POST, .handler = mode_handler};
    httpd_uri_t track_uri = {.uri = "/track", .method = HTTP_GET, .handler = track_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scan_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &mode_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &mode_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &track_uri));
    ESP_LOGI(TAG, "SoftAP %s started; poll http://192.168.4.1/scan", BTRPA_WIFI_AP_SSID);
    return ESP_OK;
}
