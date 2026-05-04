#include "ble_scanner.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"

static const char *TAG = "btrpa_scan";
static uint8_t s_own_addr_type;

static const char *addr_type_name(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC: return "public";
    case BLE_ADDR_RANDOM: return "random";
    case BLE_ADDR_PUBLIC_ID: return "public_id";
    case BLE_ADDR_RANDOM_ID: return "random_id";
    default: return "unknown";
    }
}

static const char *adv_type_name(uint8_t type)
{
    switch (type) {
    case BLE_HCI_ADV_TYPE_ADV_IND: return "adv_ind";
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD: return "adv_direct_ind";
    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND: return "adv_scan_ind";
    case BLE_HCI_ADV_TYPE_ADV_NONCONN_IND: return "adv_nonconn_ind";
    case BLE_HCI_ADV_TYPE_SCAN_RSP: return "scan_rsp";
    default: return "unknown";
    }
}

static void format_addr(const uint8_t val[6], char out[18])
{
    /* NimBLE reports little-endian over-the-air order. Print conventional MAC. */
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             val[5], val[4], val[3], val[2], val[1], val[0]);
}

static void append_json_escaped(char *out, size_t out_len, const uint8_t *data, size_t len)
{
    size_t pos = strlen(out);
    for (size_t i = 0; i < len && pos + 7 < out_len; ++i) {
        uint8_t c = data[i];
        if (c == '\\' || c == '"') {
            out[pos++] = '\\';
            out[pos++] = (char)c;
        } else if (isprint(c)) {
            out[pos++] = (char)c;
        } else {
            int n = snprintf(&out[pos], out_len - pos, "\\u%04X", c);
            if (n < 0) {
                break;
            }
            pos += (size_t)n;
        }
    }
    out[pos] = '\0';
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < out_len; ++i) {
        pos += (size_t)snprintf(&out[pos], out_len - pos, "%02X", data[i]);
    }
    out[pos] = '\0';
}

static void append_uuid(const ble_uuid_t *uuid, char *out, size_t out_len)
{
    char tmp[BLE_UUID_STR_LEN] = {0};
    ble_uuid_to_str(uuid, tmp);
    if (out[0] != '\0') {
        strlcat(out, ",", out_len);
    }
    strlcat(out, tmp, out_len);
}

static void parse_adv(const uint8_t *data, uint8_t len,
                      char *name, size_t name_len,
                      char *manufacturer, size_t manufacturer_len,
                      char *services, size_t services_len)
{
    for (uint8_t i = 0; i < len;) {
        uint8_t field_len = data[i++];
        if (field_len == 0 || i + field_len > len + 1) {
            break;
        }
        uint8_t type = data[i++];
        uint8_t value_len = field_len - 1;
        const uint8_t *value = &data[i];

        switch (type) {
        case BLE_HS_ADV_TYPE_INCOMP_NAME:
        case BLE_HS_ADV_TYPE_COMP_NAME:
            name[0] = '\0';
            append_json_escaped(name, name_len, value, value_len);
            break;
        case BLE_HS_ADV_TYPE_MFG_DATA:
            bytes_to_hex(value, value_len, manufacturer, manufacturer_len);
            break;
        case BLE_HS_ADV_TYPE_INCOMP_UUIDS16:
        case BLE_HS_ADV_TYPE_COMP_UUIDS16:
            for (uint8_t off = 0; off + 1 < value_len; off += 2) {
                ble_uuid16_t u = BLE_UUID16_INIT((uint16_t)value[off] | ((uint16_t)value[off + 1] << 8));
                append_uuid(&u.u, services, services_len);
            }
            break;
        case BLE_HS_ADV_TYPE_INCOMP_UUIDS32:
        case BLE_HS_ADV_TYPE_COMP_UUIDS32:
            for (uint8_t off = 0; off + 3 < value_len; off += 4) {
                uint32_t v = (uint32_t)value[off] | ((uint32_t)value[off + 1] << 8) |
                             ((uint32_t)value[off + 2] << 16) | ((uint32_t)value[off + 3] << 24);
                ble_uuid32_t u = BLE_UUID32_INIT(v);
                append_uuid(&u.u, services, services_len);
            }
            break;
        case BLE_HS_ADV_TYPE_INCOMP_UUIDS128:
        case BLE_HS_ADV_TYPE_COMP_UUIDS128:
            for (uint8_t off = 0; off + 15 < value_len; off += 16) {
                ble_uuid128_t u;
                memcpy(u.value, &value[off], 16);
                u.u.type = BLE_UUID_TYPE_128;
                append_uuid(&u.u, services, services_len);
            }
            break;
        default:
            break;
        }
        i = (uint8_t)(i + value_len);
    }
}

static void emit_json(const struct ble_gap_disc_desc *disc)
{
    char addr[18] = {0};
    char name[64] = {0};
    char manufacturer[128] = {0};
    char services[256] = {0};

    format_addr(disc->addr.val, addr);
    parse_adv(disc->data, disc->length_data, name, sizeof(name),
              manufacturer, sizeof(manufacturer), services, sizeof(services));

    printf("{\"address\":\"%s\",\"address_type\":\"%s\",\"rssi\":%d,"
           "\"adv_type\":\"%s\",\"local_name\":\"%s\","
           "\"manufacturer_data\":\"%s\",\"service_uuids\":\"%s\"}\n",
           addr, addr_type_name(disc->addr.type), disc->rssi,
           adv_type_name(disc->event_type), name, manufacturer, services);
    fflush(stdout);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        emit_json(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "scan complete; restarting");
        btrpa_ble_scanner_start();
        return 0;
    default:
        return 0;
    }
}

esp_err_t btrpa_ble_scanner_start(void)
{
    struct ble_gap_disc_params params = {
        .itvl = BTRPA_SCAN_INTERVAL_UNITS,
        .window = BTRPA_SCAN_WINDOW_UNITS,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = BTRPA_SCAN_ACTIVE ? 0 : 1,
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(s_own_addr_type, BTRPA_SCAN_DURATION_SECONDS,
                          &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "started %s NimBLE scan", BTRPA_SCAN_ACTIVE ? "active" : "passive");
    return ESP_OK;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    btrpa_ble_scanner_start();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t btrpa_nimble_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_device_name_set("btrpa-s3-scan");
    ble_hs_cfg.sync_cb = on_sync;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
