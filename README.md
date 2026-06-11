# OTA — VLC LED Firmware Updates

Firmware + OTA update infrastructure for ESP32 VLC transmitter.

## Release workflow

1. Go to **Actions → Build & Release Firmware → Run workflow**
2. Enter the new version (e.g. `1.1`)
3. Downloads the signed firmware from the workflow artifacts

## Manual build

```bash
python sign_update.py --embed
arduino-cli compile --fqbn esp32:esp32:esp32wrover vlc_tx
arduino-cli upload --fqbn esp32:esp32:esp32wrover --port /dev/ttyUSB0 vlc_tx
python sign_update.py vlc_tx.ino.bin -v 1.0
```

## Flashing

Hold **BOOT**, tap **EN**, release BOOT, then:

```bash
arduino-cli upload --fqbn esp32:esp32:esp32wrover --port /dev/ttyUSB0 --input-dir /tmp/build
```
