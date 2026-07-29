# ESP32-OTA-Pull Setup Guide

## Overview

HydroIoT telah terintegrasi dengan **ESP32-OTA-Pull** untuk update firmware Over-The-Air (OTA) tanpa kabel USB.

### Fitur
- ✅ WiFi ON/OFF strategy (hemat power untuk sensor nodes)
- ✅ Non-blocking (tidak mengganggu operasi normal)
- ✅ Error handling lengkap
- ✅ Auto-reboot setelah update berhasil
- ✅ Progress indicator di Serial Monitor

## Files Added/Created

| File | Lokasi | Fungsi |
|------|--------|--------|
| `ota_config.h` | `firmware/Program Utama/` | Konfigurasi OTA |
| `ota_handler.h` | `firmware/Program Utama/` | Handler module |
| `ota_manifest.json` | `firmware/` | Sample manifest |

## Files Modified

| File | Perubahan |
|------|-----------|
| `sensor_client_node2.ino` | Tambah WiFi ON/OFF + OTA |
| `sensor_client_node3.ino` | Tambah WiFi ON/OFF + OTA |
| `firebase_gateway.ino` | Tambah WiFiManager + OTA |

## Dependencies

Install library berikut di Arduino IDE:

```
1. ESP32-OTA-Pull by mikalhart
   - Sketch → Include Library → Manage Libraries
   - Search: "ESP32-OTA-Pull"
   - Install

2. ArduinoJson (dependency ESP32-OTA-Pull)
   - Search: "ArduinoJson"
   - Install

3. WiFiManager by tzapu (untuk Gateway)
   - Search: "WiFiManager"
   - Install
```

## Configuration

### 1. Edit `ota_config.h`

```cpp
// ===== FIRMWARE VERSION =====
#define FIRMWARE_VERSION    "2.1.0"     // UPDATE INI SETIAP KALI COMPILE!

// ===== WIFI CREDENTIALS =====
#define WIFI_SSID           "YOUR_WIFI_SSID"      // GANTI!
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"  // GANTI!

// ===== OTA MANIFEST URL =====
#define OTA_MANIFEST_URL    "https://example.com/firmware/manifest.json"  // GANTI!

// ===== OTA CHECK INTERVAL =====
#define OTA_CHECK_INTERVAL  3600000     // 1 jam (ms)
```

### 2. Partition Scheme (PENTING!)

Di Arduino IDE:
1. Tools → Partition Scheme
2. Pilih: **"Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"**

⚠️ **JANGAN** pilih "Huge APP" - tidak support OTA!

### 3. Compile per Device

| Device | Board | Partition |
|--------|-------|-----------|
| Node 2 | ESP32 Dev Module | Default 4MB OTA |
| Node 3 | ESP32 Dev Module | Default 4MB OTA |
| Gateway | ESP32 Dev Module | Default 4MB OTA |

## Setup OTA Server

### Option 1: GitHub Releases (Recommended)

1. Create GitHub repository
2. Upload firmware binary ke Releases
3. Upload `manifest.json` ke repository
4. Gunakan raw URL untuk manifest

**Contoh manifest.json:**
```json
{
  "Configurations": [
    {
      "Board": "ESP32_DEV",
      "Version": "2.1.0",
      "URL": "https://github.com/user/repo/releases/download/v2.1.0/sensor_node2.ino.bin"
    }
  ]
}
```

### Option 2: Any Static Web Server

1. Upload `.bin` file ke web server
2. Upload `manifest.json` ke web server
3. Pastikan HTTPS aktif

**Contoh URL:**
```
https://your-server.com/firmware/manifest.json
https://your-server.com/firmware/sensor_node2_v2.1.0.bin
```

### Option 3: Google Cloud Storage / AWS S3

1. Create bucket public
2. Upload files
3. Gunakan public URL

## Workflow Update Firmware

### 1. Compile Firmware Baru

```
1. Buka ota_config.h
2. Update FIRMWARE_VERSION (misal: "2.1.1")
3. Compile & Export Binary (Sketch → Export Compiled Binary)
4. Rename file sesuai device
```

### 2. Upload ke Server

```
1. Upload .bin file ke web server
2. Update manifest.json dengan versi baru & URL baru
3. Upload manifest.json
```

### 3. Device Auto-Update

```
1. Device check OTA setiap 1 jam (configurable)
2. Jika versi baru tersedia → download & install
3. Auto-reboot setelah install berhasil
4. Device lanjut normal dengan firmware baru
```

## LED Indicator (Optional)

Untuk menampilkan aktivitas OTA via LED:

```cpp
// Di ota_config.h
#define OTA_LED_PIN         2           // GPIO 2 (LED bawaan ESP32)
```

LED akan berkedip saat OTA check sedang berlangsung.

## Troubleshooting

### Masalah: WiFi tidak bisa connect

```
- Pastikan WIFI_SSID dan WIFI_PASSWORD benar
- Cek jarak dari router WiFi
- Restart device
```

### Masalah: OTA check gagal

```
- Pastikan manifest.json bisa diakses via HTTPS
- Cek Serial Monitor untuk error message
- Pastikan versi firmware di manifest > versi device
```

### Masalah: Device stuck setelah update

```
- Flash via USB sebagai fallback
- Cek partition scheme di Arduino IDE
- Pastikan OTA分区 tersedia
```

### Masalah: Gateway membutuhkan WiFi

```
- Gateway sekarang menggunakan WiFiManager
- Pada boot pertama, WiFi akan membuat hotspot "HydroIoT-Gateway"
- Connect ke hotspot, buka 192.168.4.1 untuk konfigurasi WiFi
- WiFi tersimpan di NVS untuk boot berikutnya
```

## Safety Features

| Feature | Deskripsi |
|---------|-----------|
| **Timeout** | OTA check dibatalkan jika melebihi timeout |
| **Retry** | Jika gagal, coba lagi pada interval berikutnya |
| **Fallback** | Device tetap berfungsi normal jika OTA gagal |
| **Version Check** | Hanya update jika versi baru tersedia |
| **No Downgrade** | Default: tidak mengizinkan downgrade versi |

## Power Consumption

### Sensor Nodes (WiFi ON/OFF)

| Mode | WiFi | Arus | Durasi |
|------|------|------|--------|
| Normal | OFF | ~20mA | 99.9% waktu |
| OTA Check | ON | ~100mA | 5-10 detik/jam |
| **Rata-rata** | — | **~20.14mA** | — |

Tambahan daya: **~0.14mA** (tidak signifikan untuk power supply)

### Gateway (WiFi Always ON)

| Mode | WiFi | Arus |
|------|------|------|
| Normal | ON (WIFI_STA) | ~100mA |
| OTA Check | ON | ~120mA |

## Manual OTA Check

Untuk memaksa check update segera, tambahkan command di Serial Monitor:

```
ota check
```

(Harap implementasi di `ota_handler.h` jika diperlukan)

## Security Notes

⚠️ **Perhatian Keamanan:**

1. Gunakan HTTPS untuk manifest URL
2. Jangan commit `ota_config.h` dengan credentials ke public repo
3. Pertimbangkan enkripsi firmware binary
4. Audit akses ke OTA server

## References

- [ESP32-OTA-Pull GitHub](https://github.com/mikalhart/ESP32-OTA-Pull)
- [Arduino ESP32 OTA](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [WiFiManager GitHub](https://github.com/tzapu/WiFiManager)
