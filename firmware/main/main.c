#include "ble_scanner.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "btrpa_main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "btrpa-scan ESP32-S3 firmware starting");
    ESP_LOGI(TAG, "JSON Lines BLE advertisements will be emitted on the ESP-IDF console");
    ESP_ERROR_CHECK(btrpa_nimble_init());
}
