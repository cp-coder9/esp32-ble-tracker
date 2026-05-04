#include "ble_gatt.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "detection_cache.h"
#include "ble_scanner.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "btrpa_gatt";
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;
static bool s_notify_enabled;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);

static int access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char payload[BTRPA_DETECTION_JSON_MAX];
        btrpa_detection_t latest;
        if (!btrpa_detection_cache_latest(&latest)) {
            snprintf(payload, sizeof(payload), "{\"source\":\"ESP32\",\"status\":\"scanning\",\"gps\":\"absent\",\"gps_source\":\"phone_fallback_supported\"}");
        } else {
            btrpa_detection_to_json(&latest, payload, sizeof(payload));
        }
        return os_mbuf_append(ctxt->om, payload, strlen(payload)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char cmd[96] = {0}; int n = OS_MBUF_PKTLEN(ctxt->om); if (n >= (int)sizeof(cmd)) n = sizeof(cmd) - 1; os_mbuf_copydata(ctxt->om, 0, n, cmd);
        if (strncmp(cmd, "mode=", 5) == 0) { btrpa_ble_mode_t m; if (btrpa_ble_parse_mode(cmd + 5, &m)) btrpa_ble_set_mode(m); }
        ESP_LOGI(TAG, "GATT RX command: %s", cmd);
        return 0;
    }
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid = &s_tx_uuid.u, .access_cb = access_cb, .val_handle = &s_tx_val_handle,
         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
        {.uuid = &s_rx_uuid.u, .access_cb = access_cb,
         .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
        {0},
     }},
    {0},
};

int btrpa_gatt_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "GATT client connected");
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_notify_enabled = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "GATT client disconnected");
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
        }
        return 0;
    default:
        return 0;
    }
}

esp_err_t btrpa_gatt_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("ESP32 Tracker");
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) return ESP_FAIL;
    return ESP_OK;
}

void btrpa_gatt_notify_latest(void)
{
    if (!s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    btrpa_detection_t latest;
    if (!btrpa_detection_cache_latest(&latest)) return;
    char payload[BTRPA_DETECTION_JSON_MAX];
    btrpa_detection_to_json(&latest, payload, sizeof(payload));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, strlen(payload));
    if (om != NULL) {
        ble_gattc_notify_custom(s_conn_handle, s_tx_val_handle, om);
    }
}
