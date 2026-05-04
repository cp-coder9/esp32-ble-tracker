#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_WIFI_AP_SSID "ESP32-BTRPA-Tracker"
#define BTRPA_WIFI_AP_PASS "btrpa1234"
#define BTRPA_WIFI_AP_CHANNEL 6
#define BTRPA_WIFI_AP_MAX_STA 2

esp_err_t btrpa_wifi_remote_start(void);

#ifdef __cplusplus
}
#endif
