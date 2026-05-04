# BTRPA Radar Android APK

Native Android implementation of the ESP32 BLE tracker UI and data model.

## Build

Install Android Studio or Android SDK plus JDK 17, then run from this directory:

```bat
gradlew.bat :app:assembleDebug
```

If using Android Studio, open this `android/` folder and run **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

Expected debug APK path after a successful build:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Implemented Android-side features

- Modern dark native UI with status chips for phone BLE, ESP32 direct BLE, ESP32 Wi-Fi remote, and GPS.
- Source and transport controls for phone scanning, ESP32 Direct GATT read/notify, and ESP32 Wi-Fi HTTP polling.
- ESP32 Bluetooth mode changer sends `/mode?value=scanner`, `/mode?value=gatt`, or `/mode?value=hybrid`. Scanner mode is the reliable real-time tracking mode; Direct GATT is for direct phone connection and may pause scanning on the ESP32 for GAP safety.
- Target tracking UI lets the user enter/select a target address or name and a near threshold in meters. Phone BLE tracking runs locally from the phone scan list; ESP32 tracking is also pushed to `/track` when ESP32 source is selected.
- Mini terminal/log popup records Wi-Fi poll results/errors, mode changes, tracking updates, GPS/app state, and direct BLE scan/connect/read/notify events. USB serial is not implemented in this APK; use ESP-IDF monitor for raw USB logs.
- Phone BLE scanning with calibrated RSSI filtering, name/MAC filtering, median RSSI, exponential moving average, sample count, RSSI noise/variance indication, estimated distance in meters, recency, confidence, and source labels.
- Distance estimates use configurable reference RSSI at 1 meter and path-loss exponent fields. Quick calibration presets are available for open-space and indoor assumptions. Defaults remain conservative: `-59 dBm` at 1 m and path-loss exponent `3.0`.
- Distance labels include honest bands: `very near`, `near`, `medium`, `far`, or `unknown`, alongside approximate meters and confidence.
- Compass sweep mode uses Android rotation vector when available, with accelerometer/magnetometer fallback. While a target is tracked, press **START COMPASS SWEEP** and rotate slowly; the app records heading plus filtered RSSI samples and reports a **strongest-signal heading** only when the RSSI peak is sufficiently above the sweep average.
- Movement hint uses phone GPS fallback observations when available. After the user walks far enough and the filtered RSSI changes enough, the UI can show hints such as `signal improving toward NE`; otherwise it prompts the user to walk farther for bearing.
- Realistic radar visualization plots detections by estimated distance, adjusts color/alpha with confidence/noise/age, draws uncertainty circles, and shows a bearing sector/cone for tracked devices only when sweep confidence is adequate. Without sweep confidence, angle remains a stable visual layout.
- Phone GPS fallback: detections get phone coordinates when ESP32 GPS coordinates are not supplied by a remote ESP32 endpoint.
- OSM map panel via osmdroid with scanner and observation markers when location metadata is available.
- Runtime permissions for Bluetooth, fine/coarse location, Internet/network state, and Wi-Fi state, with guarded BLE `SecurityException` handling around scan/connect/GATT operations.
- Android network security blocks general cleartext traffic and allows cleartext HTTP only for the ESP32 SoftAP endpoint `192.168.4.1`.

## ESP32 remote endpoint expectations

The Wi-Fi poller is the reliable real-time ESP32 transport and expects firmware NDJSON records from `/scan`. It parses JSON with Android `JSONObject` first, then falls back to simple JSON-like or CSV-like legacy lines containing fields such as:

```text
address,name,rssi,tx,lat,lon
```

or JSONL-style records with keys such as `address`, `name`, `rssi`, `tx`, `lat`, and `lon`. If `lat`/`lon` are missing, the app uses phone GPS as the location source when enabled.

Additional ESP32 controls used by the UI over Wi-Fi fallback:

- `GET /mode` returns the current Bluetooth mode.
- `GET /mode?value=scanner|gatt|hybrid` changes mode.
- `GET /track?address=AA:BB:CC:DD:EE:FF&meters=5` tracks by address.
- `GET /track?name=Beacon&meters=5` tracks by name substring.
- `GET /track?off=1` disables ESP32-side target tracking.
- `GET /status` includes mode and tracking state.

## Notes

- Android does not always expose true BLE random private addresses to apps on every device/OS build.
- Direct BLE GATT scans for the firmware Nordic-UART-shaped service, connects, reads the TX characteristic once, subscribes to TX notifications through CCCD `00002902-0000-1000-8000-00805f9b34fb`, and feeds JSON payloads through the same ingest path as Wi-Fi using source `ESP32-GATT`. RX writes send simple `mode=` commands when connected; otherwise the app falls back to Wi-Fi `/mode`.
- Wi-Fi HTTP uses cleartext only for the local ESP32 SoftAP endpoint allowed by the app network security config. Other cleartext destinations are blocked by Android policy.
- RSSI distance is an estimate affected by antenna orientation, transmit power, body blocking, walls, multipath, chipset behavior, and interference. Calibrate reference RSSI/path-loss in the same environment when possible.
- The sweep result is a **bearing hint** / **strongest-signal heading**, not true angle-of-arrival. A single standard ESP32-S3 scanner and a phone BLE receiver cannot provide real BLE AoA or a guaranteed target bearing without antenna-array hardware and compatible radio/firmware support.
- Movement hints depend on enough GPS movement plus a meaningful RSSI trend; they are rough warmer/colder guidance, not navigation-grade direction.
- OSM map tiles require network access and the osmdroid dependency from Maven Central.
