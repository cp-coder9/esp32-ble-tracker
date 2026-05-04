# btrpa-scan ESP32-S3 firmware

This directory is an ESP-IDF companion firmware project for ESP32-S3-WROOM-1-N16R8 development boards. It does not replace the host-side Python scanner in `../btrpa_scan/cli.py`; it provides a small on-device BLE observer that writes newline-delimited JSON advertisements to the ESP-IDF console.

## Current scope

- Target: ESP32-S3 (`esp32s3`) with BLE only.
- Bluetooth host: NimBLE observer role.
- Classic Bluetooth: disabled.
- Board assumptions: 16 MB flash and 8 MB Octal PSRAM defaults suitable for ESP32-S3-WROOM-1-N16R8 modules, without assuming a specific dev-board pinout.
- Output: JSON Lines on serial/USB console.

Example output:

```jsonl
{"address":"AA:BB:CC:DD:EE:FF","address_type":"random","rssi":-63,"adv_type":"adv_ind","local_name":"Beacon","manufacturer_data":"4C000215...","service_uuids":"180f"}
```

## Build and flash

ESP-IDF v5.1 or newer is recommended; v5.2/v5.3 are expected to work with the NimBLE APIs used here.

From this repository root:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

On Windows with the ESP-IDF PowerShell environment active, the same commands can be run from `firmware/`.

## Console notes

`sdkconfig.defaults` enables USB CDC console defaults for common ESP32-S3 dev boards. If your board exposes only a USB-UART bridge or if monitor output is not visible, run `idf.py menuconfig` and adjust `Component config -> ESP System Settings -> Channel for console output`, then rebuild.

## Scanner configuration

The scanner constants are in `main/ble_scanner.h`:

- `BTRPA_SCAN_ACTIVE`: `0` for passive scanning, `1` for active scan requests.
- `BTRPA_SCAN_INTERVAL_UNITS`: scan interval in 0.625 ms BLE units.
- `BTRPA_SCAN_WINDOW_UNITS`: scan window in 0.625 ms BLE units.
- `BTRPA_SCAN_DURATION_SECONDS`: `0` for continuous scanning.

The scanner parses these advertisement fields when present:

- Bluetooth address and address type.
- RSSI.
- Advertising event type.
- Complete or shortened local name.
- Manufacturer data as hexadecimal.
- 16-bit, 32-bit, and 128-bit service UUID lists.

## RPA resolver module

`main/rpa_resolver.c` and `main/rpa_resolver.h` contain a small C API for Bluetooth privacy helpers:

- `btrpa_is_rpa()` detects resolvable private addresses.
- `btrpa_ah()` implements Bluetooth `ah()` using mbedTLS AES-128-ECB.
- `btrpa_resolve_rpa()` resolves a binary address against an IRK.
- `btrpa_parse_addr()` and `btrpa_resolve_rpa_text()` support unit-test-friendly textual address handling.

This module is intentionally not wired into filtering yet. It mirrors the Python helper conceptually and is ready for future IRK-driven scanner filtering or unit tests.

## Partition table

`partitions.csv` assumes 16 MB flash and reserves:

- NVS, OTA metadata, and PHY init data.
- A 2 MB factory app slot.
- Two 3 MB OTA app slots for future update support.
- The remaining flash as SPIFFS storage.

## Validation status

If ESP-IDF is installed and exported in your shell, run `idf.py build` from this directory. In environments without ESP-IDF, validation is limited to file layout and source inspection.
