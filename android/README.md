# BTRPA Radar Android APK

Native Android implementation of the BLE/RPA scanner UI and data model.

## Build

Install Android Studio or Android SDK plus JDK 17, then run from this directory:

```bat
gradle :app:assembleDebug
```

If using Android Studio, open this `android/` folder and run **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

Expected debug APK path after a successful build:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Notes

- Android does not always expose true BLE random private addresses to apps on every device/OS build; the app displays a warning in IRK mode.
- Runtime permissions request Bluetooth scan/connect and fine location on Android 13+ compatible devices.
- Export supports CSV, JSON, and JSONL through Android share sheets.
