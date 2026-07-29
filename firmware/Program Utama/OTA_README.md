# ESP32-OTA-Pull Integration - File Structure

## Overview

ESP32-OTA-Pull has been integrated into all ESP32 firmware files for over-the-air updates. Each node has its own copy of OTA configuration and handler files to ensure Arduino IDE compatibility.

## File Structure

```
hidroponik-iot/firmware/Program Utama/
├── sensor_client_node2/
│   ├── sensor_client_node2.ino    ← Main firmware
│   ├── ota_config.h               ← OTA configuration
│   └── ota_handler.h              ← OTA handler class
├── sensor_client_node3/
│   ├── sensor_client_node3.ino    ← Main firmware
│   ├── ota_config.h               ← OTA configuration
│   └── ota_handler.h              ← OTA handler class
├── firebase_gateway/
│   ├── firebase_gateway.ino       ← Main firmware
│   ├── ota_config.h               ← OTA configuration (WiFi Always ON)
│   └── ota_handler.h              ← OTA handler class
└── OTA_README.md                  ← This file
```

## ⚠️ Important: Keep Files in Sync

When updating OTA configuration, you must edit **all 3 copies** of `ota_config.h` to keep them synchronized:

- `sensor_client_node2/ota_config.h`
- `sensor_client_node3/ota_config.h`
- `firebase_gateway/ota_config.h`

The only exception is `FIRMWARE_VERSION` which should be the same for all nodes.

## Why Separate Files?

Arduino IDE compiles each `.ino` file as a separate project. It does **not** support:
- Relative path includes like `#include "../ota_handler.h"`
- Shared source files across multiple sketch directories

Therefore, each node must have its own copy of `ota_handler.h` and `ota_config.h`.

## Configuration Files

### ota_config.h

Configure these values before uploading firmware:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `FIRMWARE_VERSION` | `"2.1.0"` | Current firmware version (semver format) |
| `OTA_DEVICE_ID` | `""` | Device ID for targeted updates |
| `OTA_MANIFEST_URL` | `"https://example.com/firmware/manifest.json"` | URL to manifest JSON |
| `OTA_CHECK_INTERVAL` | `3600000` (1 hour) | How often to check for updates (ms) |
| `WIFI_SSID` | `"YOUR_WIFI_SSID"` | WiFi network name |
| `WIFI_PASSWORD` | `"YOUR_WIFI_PASSWORD"` | WiFi password |
| `OTA_INITIAL_DELAY` | `30000` (30s) | Delay before first OTA check (ms) |

### ota_handler.h

Contains the `OTAHandler` class with WiFi ON/OFF strategy for sensor nodes.

## WiFi Strategy

### Sensor Nodes (Node 2 & 3)
```
WiFi: OFF → [ON 5-10s] → OFF → [ON 5-10s] → OFF...
                    ↑ OTA Check (every hour)
```
- WiFi only enabled during OTA check
- Minimizes power consumption (~0.2 mA average)

### Gateway
```
WiFi: Always ON (via WiFiManager)
```
- Gateway already has WiFi for Firebase bridge
- OTA check runs in background

## OTA Workflow

```
Boot → Wait 30-60s → Connect WiFi → Check Manifest → Download if newer → Install → Reboot
```

## How to Update Firmware

1. **Increment `FIRMWARE_VERSION`** in `ota_config.h` for all nodes
2. **Create new firmware binary** and upload to your hosting server
3. **Update `manifest.json`** with new version info
4. **Upload manifest and binary** to your HTTPS server
5. **Nodes will automatically detect and update** within the check interval

## Required Libraries

Install these via Arduino Library Manager:

```
ESP32-OTA-Pull by mikalhart
ArduinoJson by Benoit Blanchon
```

## Partition Scheme

**IMPORTANT**: In Arduino IDE, select a partition scheme that supports OTA:

- ✅ `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
- ✅ `Minimal SPIFFS (1.3MB APP/700KB SPIFFS)`
- ❌ `Huge APP (3MB No OTA/1MB SPIFFS)` - Does NOT support OTA!

## Manifest JSON Format

```json
{
  "files": [
    {
      "version": "2.1.0",
      "device": "node2",
      "url": "https://example.com/firmware/node2_v2.1.0.bin",
      "size": 1234567,
      "md5": "abc123..."
    }
  ]
}
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| OTA check not starting | Verify WiFi credentials in `ota_config.h` |
| Manifest fetch fails | Ensure URL is HTTPS and server is accessible |
| Update not installing | Check partition scheme supports OTA |
| Device reboot loops | Verify firmware version is higher than current |
