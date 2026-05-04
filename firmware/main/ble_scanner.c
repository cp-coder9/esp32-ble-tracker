#include "ble_scanner.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ble_gatt.h"
#include "detection_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/hci_common.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"

void ble_store_config_init(void);
int btrpa_gatt_gap_event(struct ble_gap_event *event, void *arg);

static const char *TAG = "btrpa_scan";
static uint8_t s_own_addr_type;
static btrpa_ble_mode_t s_mode = BTRPA_BLE_MODE_SCANNER;
static bool s_synced;
static bool s_gatt_ready;

#ifndef BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP 0x04
#endif

static int combined_gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void);

const char *btrpa_ble_mode_name(btrpa_ble_mode_t mode) { switch (mode) { case BTRPA_BLE_MODE_SCANNER: return "scanner"; case BTRPA_BLE_MODE_GATT: return "gatt"; case BTRPA_BLE_MODE_HYBRID: return "hybrid"; default: return "scanner"; } }
bool btrpa_ble_parse_mode(const char *value, btrpa_ble_mode_t *out) { if (!value || !out) return false; if (strcasecmp(value,"scanner")==0 || strcasecmp(value,"scan")==0) *out=BTRPA_BLE_MODE_SCANNER; else if (strcasecmp(value,"gatt")==0 || strcasecmp(value,"direct")==0 || strcasecmp(value,"connectable")==0) *out=BTRPA_BLE_MODE_GATT; else if (strcasecmp(value,"hybrid")==0 || strcasecmp(value,"auto")==0) *out=BTRPA_BLE_MODE_HYBRID; else return false; return true; }
btrpa_ble_mode_t btrpa_ble_get_mode(void) { return s_mode; }

static void schedule_advertise(struct ble_npl_event *ev)
{
    (void)ev;
    advertise();
}

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
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP: return "scan_rsp";
    default: return "unknown";
    }
}

static void format_addr(const uint8_t val[6], char out[18])
{
    /* NimBLE reports little-endian over-the-air order. Print conventional MAC. */
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             val[5], val[4], val[3], val[2], val[1], val[0]);
}

static void append_printable_text(char *out, size_t out_len, const uint8_t *data, size_t len)
{
    size_t pos = strlen(out);
    for (size_t i = 0; i < len && pos + 1 < out_len; ++i) {
        uint8_t c = data[i];
        if (isprint(c)) {
            out[pos++] = (char)c;
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
        if (field_len == 0) {
            break;
        }
        if (field_len < 1 || i + field_len > len) {
            break;
        }
        uint8_t type = data[i++];
        uint8_t value_len = field_len - 1;
        const uint8_t *value = &data[i];

        switch (type) {
        case BLE_HS_ADV_TYPE_INCOMP_NAME:
        case BLE_HS_ADV_TYPE_COMP_NAME:
            name[0] = '\0';
            append_printable_text(name, name_len, value, value_len);
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
    btrpa_detection_t detection = {0};

    format_addr(disc->addr.val, detection.address);
    strlcpy(detection.address_type, addr_type_name(disc->addr.type), sizeof(detection.address_type));
    strlcpy(detection.adv_type, adv_type_name(disc->event_type), sizeof(detection.adv_type));
    parse_adv(disc->data, disc->length_data, detection.name, sizeof(detection.name),
              detection.manufacturer_data, sizeof(detection.manufacturer_data),
              detection.service_uuids, sizeof(detection.service_uuids));
    detection.rssi = disc->rssi;
    detection.tx = -59;
    detection.first_seen_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    detection.last_seen_ms = detection.first_seen_ms;
    detection.has_gps = false;
    btrpa_detection_cache_upsert(&detection);
    float distance_m = 0;
    bool near = false;
    if (btrpa_track_update(&detection, &distance_m, &near)) {
        ESP_LOGI(TAG, "tracking target matched %s/%s rssi=%d distance=%.1fm state=%s", detection.address, detection.name, detection.rssi, distance_m, near ? "near" : "out");
    }

    char payload[BTRPA_DETECTION_JSON_MAX];
    btrpa_detection_to_json(&detection, payload, sizeof(payload));
    printf("%s\n", payload);
    fflush(stdout);
    btrpa_gatt_notify_latest();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        emit_json(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "scan complete; mode=%s", btrpa_ble_mode_name(s_mode));
        if (s_mode != BTRPA_BLE_MODE_GATT) btrpa_ble_scanner_start();
        return 0;
    default:
        return btrpa_gatt_gap_event(event, arg);
    }
}

static void advertise(void)
{
#if BTRPA_DIRECT_GATT_EXPERIMENTAL
    struct ble_hs_adv_fields fields = {0};
    uint8_t adv_data[BLE_HS_ADV_MAX_SZ];
    uint8_t adv_len = 0;
    const char *name = ble_svc_gap_device_name();
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    int rc = ble_hs_adv_set_fields(&fields, adv_data, &adv_len, sizeof(adv_data));
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_adv_set_fields failed: %d", rc);
        return;
    }
    rc = ble_gap_adv_set_data(adv_data, adv_len);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_set_data failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, combined_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "direct GATT advertising started");
    }
#else
    ESP_LOGI(TAG, "direct BLE GATT advertising disabled; Wi-Fi HTTP + BLE scanning is the default transport");
#endif
}

static int combined_gap_event(struct ble_gap_event *event, void *arg)
{
    int rc = gap_event(event, arg);
    if (event->type == BLE_GAP_EVENT_DISCONNECT ||
        (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status != 0)) {
        static struct ble_npl_event adv_ev;
        ble_npl_event_init(&adv_ev, schedule_advertise, NULL);
        ble_npl_eventq_put(ble_npl_eventq_dflt_get(), &adv_ev);
    }
    return rc;
}

esp_err_t btrpa_ble_scanner_start(void)
{
    if (s_mode == BTRPA_BLE_MODE_GATT) {
        ESP_LOGI(TAG, "scanner paused because mode=gatt");
        return ESP_OK;
    }
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

esp_err_t btrpa_ble_set_mode(btrpa_ble_mode_t mode)
{
    s_mode = mode;
    ESP_LOGI(TAG, "BLE mode changed to %s", btrpa_ble_mode_name(mode));
    if (!s_synced) return ESP_OK;
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    if (mode == BTRPA_BLE_MODE_GATT) advertise();
    else { if (mode == BTRPA_BLE_MODE_HYBRID) ESP_LOGW(TAG, "hybrid uses safe scanner-first behavior; advertising waits until scanner is stopped"); btrpa_ble_scanner_start(); }
    return ESP_OK;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    s_synced = true;
    if (!s_gatt_ready) { btrpa_gatt_init(); s_gatt_ready = true; }
    btrpa_ble_set_mode(s_mode);
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
    ble_hs_cfg.sync_cb = on_sync;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
