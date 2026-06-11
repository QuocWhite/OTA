# OTA — VLC LED Firmware Over-the-Air Updates

Firmware + OTA update infrastructure for ESP32 VLC transmitter.

## How OTA works

1. ESP32 boots, connects to WiFi ("Quoc" or "Cong Thanh")
2. Every ~10 seconds, fetches `dist/v{current_version}/version.json` from GitHub
3. Compares remote version with local version
4. If remote > local: downloads `firmware.bin`, verifies SHA256 + RSA signature, applies update

## Initial flash (required once)

The ESP32 needs the initial firmware flashed via serial (BOOT+EN):

```bash
arduino-cli upload --fqbn esp32:esp32:esp32wrover --port /dev/ttyUSB0 --input-dir /tmp/esp32_build
```

**Steps**: Hold **BOOT** → tap **EN** → release BOOT → run command above.

After this, all future updates are OTA via GitHub.

## Creating a new release

### Option A: GitHub Actions (recommended)

1. Go to https://github.com/QuocWhite/OTA/actions/workflows/build.yml
2. Click **Run workflow** → enter version (e.g. `1.1`)
3. Download the signed firmware from workflow artifacts
4. Push the generated `dist/v{version}/` to the repo:

```bash
git add dist/v{version}/
git commit -m "Release v{version}"
git push
```

### Option B: Manual build

```bash
# 1. Embed public key into firmware
python sign_update.py --embed

# 2. Compile
arduino-cli compile --fqbn esp32:esp32:esp32wrover --output-dir /tmp/build vlc_tx

# 3. Sign
python sign_update.py /tmp/build/vlc_tx.ino.bin -v {version}

# 4. Push dist to GitHub
git add dist/v{version}/
git commit -m "Release v{version}"
git push
```

## Required GitHub secret (for Actions)

Add the RSA private key as a repository secret:

https://github.com/QuocWhite/OTA/settings/secrets/actions

- **Name**: `FIRMWARE_SIGNING_KEY`
- **Value**: content of `private.key`

```bash
# To view the key:
cat private.key
```

## How the ESP32 checks for updates

In `firmware.cpp::loop()`:
- Every ~10 seconds when WiFi is connected
- Fetches: `https://raw.githubusercontent.com/QuocWhite/OTA/main/dist/v{version}/version.json`
- Verifies SHA256 + RSA-2048 signature before applying
- On failure: logs error, skips update, retries next cycle

## Current version: v1.0

- VLC mode: timer-based frame transmission at 4kHz (default)
- Manual mode: LEDC PWM dimming at 20kHz fixed frequency (no flicker)
- Commands: `VLC`, `LED N ON/OFF`, `DIM N`, `FREQ N`
