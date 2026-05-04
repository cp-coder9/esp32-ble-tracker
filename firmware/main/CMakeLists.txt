idf_component_register(
    SRCS
        "main.c"
        "ble_scanner.c"
        "rpa_resolver.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        nvs_flash
        mbedtls
)
