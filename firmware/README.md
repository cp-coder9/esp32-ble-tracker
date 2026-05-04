# btrpa-scan ESP32-S3 firmware

This directory is an ESP-IDF companion firmware project for ESP32-S3-WROOM-1-N16R8 development boards. It preserves the original on-device BLE observer and exposes recent detections to the Android app over ESP32 SoftAP HTTP and Direct BLE GATT read/notify transport selected at runtime with a safe mode changer.

## Current scope

- Target: ESP32-S3 (`esp32s3`) with NimBLE.
- Bluetooth roles: observer/scanner by default, plus runtime selectable Direct GATT peripheral mode. Scanner mode is default and reliable for tracking; GATT mode advertises/connects and pauses scanning for GAP safety; Hybrid is safe scanner-first behavior.
- Wi-Fi mode: SoftAP with a small HTTP server; no station credential provisioning is required.
- Board assumptions: 16 MB flash and 8 MB Octal PSRAM defaults suitable for ESP32-S3-WROOM-1-N16R8 modules, without assuming a specific dev-board pinout.
- Outputs: JSON Lines on serial/USB console and newline-delimited JSON over HTTP.

## Serial console output

Existing serial behavior is preserved. Each BLE advertisement still emits a JSON Line on the ESP-IDF console:

```jsonl
{"address":"AA:BB:CC:DD:EE:FF","address_type":"random","rssi":-63,"adv_type":"adv_ind","local_name":"Beacon","manufacturer_data":"4C000215...","service_uuids":"180f"}
```

## Android Wi-Fi polling

On boot the firmware starts a SoftAP and HTTP server:

- SSID: `ESP32-BTRPA-Tracker`
- Password: `btrpa1234`
- AP address: `192.168.4.1`
- Detection endpoint: `http://192.168.4.1/scan`
- Status endpoint: `http://192.168.4.1/status`
- Mode endpoint: `http://192.168.4.1/mode?value=scanner|gatt|hybrid`
- Tracking endpoint: `http://192.168.4.1/track?address=AA:BB:CC:DD:EE:FF&meters=5`

`/scan` returns newline-delimited JSON (`application/x-ndjson`) from a shared recent-detection cache. The Android app can poll its default URL, `http://192.168.4.1/scan`, and parse each line independently.

The Android companion restricts cleartext HTTP to the local ESP32 SoftAP endpoint `192.168.4.1`; other cleartext hosts are blocked by Android network security policy.

Example `/scan` line:

```json
{"address":"AA:BB:CC:DD:EE:FF","name":"Beacon","local_name":"Beacon","rssi":-63,"tx":-59,"timestamp":123456,"source":"ESP32","gps":"absent","gps_source":"phone_fallback_supported","address_type":"random","adv_type":"adv_ind","manufacturer_data":"4C000215...","service_uuids":"180f"}
```

The firmware has no GPS hardware integration yet. Therefore `lat` and `lon` are omitted, `gps` is `absent`, and `gps_source` is `phone_fallback_supported`. This is intentional so the Android app can use phone GPS as the scanner-location fallback.

## Bluetooth mode changer

The firmware starts in `scanner` mode. This continuously scans BLE advertisements, emits JSON on serial, keeps the `/scan` cache fresh, and is the recommended tracker mode.

Runtime controls:

- `GET /mode` returns `{"mode":"scanner"}` or the active mode.
- `GET /mode?value=scanner` stops advertising if needed and resumes continuous scanning.
- `GET /mode?value=gatt` stops scanning and starts connectable Direct GATT advertising as `ESP32 Tracker`.
- `GET /mode?value=hybrid` uses safe scanner-first behavior. It does not force unsafe simultaneous legacy scanning and advertising.

Mode changes are logged to the ESP-IDF console. Direct GATT can also receive a simple RX command such as `mode=scanner`, `mode=gatt`, or `mode=hybrid`.

## Target tracking endpoint

ESP32-side target tracking is configured over HTTP:

- `GET /track?address=AA:BB:CC:DD:EE:FF&meters=5`
- `GET /track?name=Beacon&meters=5`
- `GET /track?rssi=-65&address=AA:BB:CC:DD:EE:FF` to use an RSSI threshold condition.
- `GET /track?off=1` to disable tracking.

Tracking state is included in `/status` and returned by `/track`. `/track` validates MAC addresses, meter thresholds, and RSSI thresholds and returns JSON `400 Bad Request` errors for invalid query values. When a matched target is seen, firmware estimates distance from RSSI using the same `tx=-59` default reference and logs whether the target is `near` or `out`. Distance is approximate and should not be treated as calibrated ranging.

## Direct BLE GATT protocol

The firmware contains a Nordic-UART-shaped service aligned with the Android constants and built with `BTRPA_DIRECT_GATT_EXPERIMENTAL` enabled. It is still controlled safely at runtime: `scanner` mode does not advertise, while `gatt` mode advertises and pauses scanning. Wi-Fi HTTP polling at `/scan` remains the recommended active scanner/tracker transport when continuous scanning is needed.

- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX/write characteristic UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX/read/notify characteristic UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

Reading the TX characteristic returns the latest detection JSON, or a status JSON object if the cache is empty. Subscribing to notifications on TX sends the latest JSON payload when new advertisements are observed. The GATT server resets connection/notification state on disconnect and only notifies while a client is connected and subscribed. RX writes accept simple mode commands (`mode=scanner`, `mode=gatt`, `mode=hybrid`).

## Shared detection cache

`main/detection_cache.c` stores up to 64 recent BLE devices keyed by Bluetooth address. Serial output, the HTTP endpoint, tracking status JSON, and GATT reads/notifications are all fed from the same scan path so the transports stay consistent. String fields are JSON-escaped before formatting transport payloads.

The scanner parses these advertisement fields when present:

- Bluetooth address and address type.
- RSSI.
- Advertising event type.
- Complete or shortened local name.
- Manufacturer data as hexadecimal.
- 16-bit, 32-bit, and 128-bit service UUID lists.

## RSSI distance limitations

The firmware reports RSSI and a default `tx` reference of `-59` dBm for Android-side distance estimation. BLE RSSI distance is only an approximation and varies heavily with antenna orientation, walls, body blocking, transmit power, and multipath. A single ESP32 receiver cannot determine real bearing/angle; radar angle must be treated as a stable visual layout rather than physical direction.

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

Run `idf.py build` from this directory after ESP-IDF is exported in your shell. In environments without ESP-IDF, validation is limited to source inspection.
