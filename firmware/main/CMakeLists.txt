idf_component_register(
    SRCS
        "main.c"
        "ble_scanner.c"
        "ble_gatt.c"
        "detection_cache.c"
        "rpa_resolver.c"
        "wifi_remote.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        esp_http_server
        esp_wifi
        nvs_flash
        mbedtls
        esp_timer
)
