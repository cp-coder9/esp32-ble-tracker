# BTRPA ESP32-S3 web loader

Static browser flasher for the ESP32-S3 firmware using ESP Web Tools and Web Serial in Chrome/Edge.

## Files

- `index.html` - browser UI with the ESP Web Tools install button.
- `manifest.json` - ESP Web Tools manifest.
- `firmware/bootloader.bin` - bootloader image at offset `0x0`.
- `firmware/partition-table.bin` - partition table image at offset `0x8000`.
- `firmware/ota_data_initial.bin` - OTA data image at offset `0xf000`.
- `firmware/btrpa_scan_firmware.bin` - app image at offset `0x20000`.

The offsets were derived from `firmware/build/flasher_args.json`.

## Usage

1. Start a static server from this directory:

   ```bat
   cd web-loader
   python -m http.server 8000
   ```

2. Open `http://localhost:8000/` in current Chrome or Edge on desktop.
3. Connect the ESP32-S3 by USB. If needed, hold BOOT while resetting/plugging in to enter download mode.
4. Click **Connect and flash ESP32-S3**, select the serial port, and follow the prompts.

No release signing or cloud hosting is required; the page can be served locally.
