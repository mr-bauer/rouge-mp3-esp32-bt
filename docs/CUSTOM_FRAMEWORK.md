# Custom arduino-esp32 Framework

Rouge uses a custom-compiled arduino-esp32 framework instead of the stock PlatformIO package. This was necessary to free enough IRAM for deep sleep and to enable the power management API.

---

## Background

The stock arduino-esp32 2.0.17 (IDF 4.4.x) leaves only ~1,041 bytes of IRAM free in this firmware. `esp_deep_sleep_start()` requires ~3,640 bytes for its power-down sequence, making deep sleep impossible without modifying the framework's sdkconfig.

The root cause: the BLE stack and FreeRTOS internals are compiled into IRAM by default, filling the 128KB segment even though BLE is never used in this project.

---

## What Changed

The following sdkconfig options were changed from the stock defaults:

| Config | Change | Effect |
|--------|--------|--------|
| `CONFIG_BT_BLE_ENABLED` | disabled | Removes BLE stack from IRAM; Classic BT / A2DP still works |
| `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` | enabled | Moves FreeRTOS internals from IRAM to flash; frees ~10–20 KB |
| `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH` | enabled | Same for ring buffer code |
| `CONFIG_PM_ENABLE` | enabled | Enables `esp_pm_configure()` power management API |

**Result**: ~9,808 bytes IRAM free (up from ~1,041 bytes).

---

## Setup

The custom framework lives at:
```
~/Documents/PlatformIO/custom-arduino-esp32/
```

It is pointed to via `platformio_override.ini` (gitignored — not committed to the repo):
```ini
[common]
platform_packages =
    framework-arduinoespressif32 @ symlink:///path/to/your/custom-arduino-esp32
```

Create this file at the project root and set the path to wherever you placed the custom framework. The `package.json` inside the framework directory has its version set to `99.0.0+sha.custom` to prevent PlatformIO from resolving the stock cached package instead.

---

## Rebuilding

The framework was built using [esp32-arduino-lib-builder](https://github.com/espressif/esp32-arduino-lib-builder) on branch `release/v4.4`, with Docker.

Source is at:
```
~/Documents/PlatformIO/esp32-arduino-lib-builder/
```

Several component fixes were required to build against IDF 4.4 (components had drifted to IDF 5.x on their master branches):
- `components/esp32-camera` — pinned to commit `1923553` (before `esp_mm` / `esp_jpeg` were introduced)
- `components/esp_littlefs` — pinned to commit `71e5492` (last commit before IDF 4.4 support was dropped)
- `components/arduino_tinyusb/.../tinyusb_src/CMakeLists.txt` — `PRIV_REQUIRES esp_mm` removed
- `CMakeLists.txt` — esp-rainmaker excluded entirely (`set(EXTRA_COMPONENT_DIRS "")`)
- All `idf_component.yml` files deleted to prevent IDF component manager version conflicts

To rebuild:

```bash
cd ~/Documents/PlatformIO/esp32-arduino-lib-builder

# Install jq (needed by build.sh — not included in the Docker image)
# Pull the IDF 4.4 build image
docker pull espressif/idf:release-v4.4

# Run the build for ESP32 only
docker run --rm -v $PWD:/build -w /build \
  espressif/idf:release-v4.4 \
  bash -c "apt-get update -qq && apt-get install -y -qq jq && IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1 bash ./build.sh -t esp32"

# Copy built SDK into the custom framework
cp -r out/tools/* ~/Documents/PlatformIO/custom-arduino-esp32/tools/
```

Build takes 45–90 minutes on first run.

---

## Rolling Back to Stock

Remove the `platform_packages` block from `platformio.ini`:

```ini
; Delete these lines to revert to stock:
; platform_packages =
;     framework-arduinoespressif32 @ symlink:///Users/mdbauer/Documents/PlatformIO/custom-arduino-esp32
```

PlatformIO will automatically re-download the official `framework-arduinoespressif32` package on the next `pio run`. No other changes needed.
