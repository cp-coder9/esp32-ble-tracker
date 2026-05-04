#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_SCAN_ACTIVE 0
#define BTRPA_SCAN_INTERVAL_UNITS 0x0060
#define BTRPA_SCAN_WINDOW_UNITS 0x0030
#define BTRPA_SCAN_DURATION_SECONDS 0

esp_err_t btrpa_nimble_init(void);
esp_err_t btrpa_ble_scanner_start(void);

#ifdef __cplusplus
}
#endif
