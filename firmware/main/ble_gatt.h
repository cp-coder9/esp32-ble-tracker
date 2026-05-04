#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_GATT_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BTRPA_GATT_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BTRPA_GATT_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

esp_err_t btrpa_gatt_init(void);
void btrpa_gatt_notify_latest(void);

#ifdef __cplusplus
}
#endif
