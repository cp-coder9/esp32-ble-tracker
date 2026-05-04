#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_SCAN_ACTIVE 0
#define BTRPA_SCAN_INTERVAL_UNITS 0x0060
#define BTRPA_SCAN_WINDOW_UNITS 0x0030
#define BTRPA_SCAN_DURATION_SECONDS 0
#define BTRPA_DIRECT_GATT_EXPERIMENTAL 1

typedef enum { BTRPA_BLE_MODE_SCANNER = 0, BTRPA_BLE_MODE_GATT = 1, BTRPA_BLE_MODE_HYBRID = 2 } btrpa_ble_mode_t;

esp_err_t btrpa_ble_set_mode(btrpa_ble_mode_t mode);
btrpa_ble_mode_t btrpa_ble_get_mode(void);
const char *btrpa_ble_mode_name(btrpa_ble_mode_t mode);
bool btrpa_ble_parse_mode(const char *value, btrpa_ble_mode_t *out);

esp_err_t btrpa_nimble_init(void);
esp_err_t btrpa_ble_scanner_start(void);

#ifdef __cplusplus
}
#endif
