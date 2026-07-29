# 📡 Panduan Penggunaan OTA (Over-The-Air) Update - HydroIoT

## 📋 Daftar Isi

1. [Overview](#overview)
2. [Arsitektur Sistem](#arsitektur-sistem)
3. [File Struktur](#file-struktur)
4. [Instalasi Library](#instalasi-library)
5. [Konfigurasi](#konfigurasi)
6. [Setup Server OTA](#setup-server-ota)
7. [Cara Update Firmware](#cara-update-firmware)
8. [Troubleshooting](#troubleshooting)
9. [FAQ](#faq)

---

## Overview

ESP32-OTA-Pull memungkinkan update firmware jarak jauh tanpa kabel USB. Sistem ini mendukung:

- ✅ Update otomatis berdasarkan interval waktu
- ✅ WiFi ON/OFF untuk sensor nodes (hemat daya)
- ✅ WiFi Always ON untuk gateway
- ✅ Fallback ke Serial-Only mode jika WiFi gagal
- ✅ Rollback otomatis jika update gagal

---

## Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────────┐
│                    OTA UPDATE FLOW                          │
│                                                             │
│  ESP32 Node → WiFi ON → Check Manifest → Download → Install │
│       ↓                                         ↓           │
│  LoRa Normal                              Reboot → Normal  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    WiFi STRATEGY                            │
│                                                             │
│  Sensor Nodes (Node 2 & 3):                                │
│    WiFi: OFF → [ON 5-10s] → OFF → [ON 5-10s] → OFF...    │
│                      ↑ OTA Check (every hour)               │
│                                                             │
│  Gateway:                                                   │
│    WiFi: Always ON (via WiFiManager)                       │
│    OTA: Runs in background                                  │
└─────────────────────────────────────────────────────────────┘
```

---

## File Struktur

```
hidroponik-iot/firmware/Program Utama/
├── sensor_client_node2/
│   ├── sensor_client_node2.ino    ← Firmware Node 2
│   ├── ota_config.h               ← Konfigurasi OTA
│   └── ota_handler.h              ← Handler OTA
├── sensor_client_node3/
│   ├── sensor_client_node3.ino    ← Firmware Node 3
│   ├── ota_config.h               ← Konfigurasi OTA
│   └── ota_handler.h              ← Handler OTA
├── firebase_gateway/
│   ├── firebase_gateway.ino       ← Firmware Gateway
│   ├── ota_config.h               ← Konfigurasi OTA (WiFi Always ON)
│   └── ota_handler.h              ← Handler OTA
└── ota_manifest.json              ← Template manifest
```

---

## Instalasi Library

### Arduino IDE

1. Buka Arduino IDE
2. Buka **Sketch → Include Library → Manage Libraries**
3. Cari dan install:
   - `ESP32-OTA-Pull` by mikalhart
   - `ArduinoJson` by Benoit Blanchon
   - `WiFiManager` by tzapu (untuk gateway)

### Partition Scheme

**PENTING**: Pilih partition scheme yang support OTA:

1. Buka **Tools → Partition Scheme**
2. Pilih salah satu:
   - ✅ `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
   - ✅ `Minimal SPIFFS (1.3MB APP/700KB SPIFFS)`
   - ❌ `Huge APP (3MB No OTA/1MB SPIFFS)` - **TIDAK SUPPORT OTA!**

---

## Konfigurasi

### File: ota_config.h

Edit file `ota_config.h` di setiap node directory:

```cpp
// ===== FIRMWARE VERSION =====
// Update versi ini setiap kali compile firmware baru
#define FIRMWARE_VERSION    "2.1.0"

// ===== DEVICE IDENTIFICATION =====
// Device ID untuk targeting di manifest JSON
// Kosongkan "" untuk update semua device
#define OTA_DEVICE_ID       "node2"  // atau "node3", "gateway"

// ===== OTA MANIFEST URL =====
// URL ke file JSON manifest
#define OTA_MANIFEST_URL    "https://yourserver.com/firmware/manifest.json"

// ===== OTA CHECK INTERVAL =====
// Interval pengecekan update (ms)
#define OTA_CHECK_INTERVAL  3600000  // 1 jam

// ===== WIFI CREDENTIALS =====
// WiFi hanya aktif saat OTA check (untuk sensor nodes)
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
```

### Parameter Penting

| Parameter | Default | Deskripsi |
|-----------|---------|-----------|
| `FIRMWARE_VERSION` | `"2.1.0"` | Versi firmware saat ini (semver) |
| `OTA_DEVICE_ID` | `""` | ID device untuk targeting update |
| `OTA_MANIFEST_URL` | `"https://..."` | URL ke manifest JSON |
| `OTA_CHECK_INTERVAL` | `3600000` (1 jam) | Interval pengecekan (ms) |
| `WIFI_SSID` | `"YOUR_WIFI_SSID"` | Nama WiFi |
| `WIFI_PASSWORD` | `"YOUR_WIFI_PASSWORD"` | Password WiFi |
| `OTA_INITIAL_DELAY` | `30000` (30s) | Delay awal sebelum cek OTA |
| `OTA_TIMEOUT` | `120000` (2 menit) | Timeout total OTA process |

---

## Setup Server OTA

### Langkah 1: Buat Manifest JSON

Buat file `manifest.json`:

```json
{
  "files": [
    {
      "version": "2.1.0",
      "device": "node2",
      "url": "https://yourserver.com/firmware/node2_v2.1.0.bin",
      "size": 1234567,
      "md5": "abc123def456..."
    },
    {
      "version": "2.1.0",
      "device": "node3",
      "url": "https://yourserver.com/firmware/node3_v2.1.0.bin",
      "size": 1234567,
      "md5": "abc123def456..."
    },
    {
      "version": "2.1.0",
      "device": "gateway",
      "url": "https://yourserver.com/firmware/gateway_v2.1.0.bin",
      "size": 1234567,
      "md5": "abc123def456..."
    }
  ]
}
```

### Langkah 2: Compile Firmware

1. Buka Arduino IDE
2. Buka file `.ino` yang ingin di-compile
3. Pilih board: **Tools → Board → ESP32 Dev Module**
4. Pilih partition scheme yang support OTA
5. Klik **Sketch → Export Compiled Binary**
6. Firmware binary akan tersimpan di folder sketch

### Langkah 3: Upload ke Server

1. Upload manifest.json ke server HTTPS
2. Upload firmware binary ke server HTTPS
3. Update URL di `ota_config.h` pada semua node

### Langkah 4: Update Versi

Untuk setiap update firmware:

1. Increment `FIRMWARE_VERSION` di `ota_config.h`:
   ```cpp
   #define FIRMWARE_VERSION    "2.2.0"  // Update dari 2.1.0
   ```

2. Compile firmware baru

3. Upload binary baru ke server

4. Update manifest.json dengan versi baru

5. Nodes akan otomatis detect dan update dalam 1 jam

---

## Cara Update Firmware

### Update Otomatis

Nodes akan otomatis check update setiap `OTA_CHECK_INTERVAL` (default: 1 jam):

1. WiFi aktif (sensor nodes) atau sudah aktif (gateway)
2. Connect ke WiFi
3. Download manifest JSON
4. Bandingkan versi
5. Jika ada versi baru → download & install
6. Reboot otomatis

### Monitor Proses OTA

Buka Serial Monitor (115200 baud) untuk melihat proses:

```
[OTA] === Memulai pengecekan update ===
[OTA] Menghubungkan WiFi...
[OTA] WiFi terhubung: 192.168.1.100
[OTA] Mengecek update...
[OTA] Progress: 50000/1234567 (4%)
[OTA] Progress: 100000/1234567 (8%)
...
[OTA] ✅ Update berhasil! Reboot dalam 3 detik...
```

### Force Check Update

Untuk memaksa check update segera, tambahkan di code:

```cpp
otaHandler.forceCheck();
```

Atau gunakan perintah Serial (gateway only):

```
forceota
```

---

## Troubleshooting

### Masalah: WiFi tidak bisa connect

**Solusi:**
1. Cek `WIFI_SSID` dan `WIFI_PASSWORD` di `ota_config.h`
2. Pastikan WiFi aktif dan jangkauan cukup
3. Gateway akan masuk Serial-Only mode jika WiFi gagal

### Masalah: Manifest tidak bisa diakses

**Solusi:**
1. Pastikan URL HTTPS valid
2. Cek SSL certificate di server
3. Pastikan manifest.json bisa diakses dari browser

### Masalah: Update gagal install

**Solusi:**
1. Cek partition scheme (harus support OTA)
2. Pastikan firmware size tidak melebihi available space
3. Cek MD5 hash di manifest

### Masalah: Device boot loop setelah update

**Solusi:**
1. Flash firmware via USB untuk recovery
2. Cek apakah firmware binary corrupt
3. Pastikan partition scheme benar

### Masalah: OTA tidak jalan

**Solusi:**
1. Cek apakah WiFi connected
2. Cek `OTA_CHECK_INTERVAL` (mungkin terlalu lama)
3. Gunakan `otaHandler.forceCheck()` untuk test

---

## FAQ

### Q: Berapa daya yang dibutuhkan untuk OTA?

**A:**
- Sensor nodes: Tambahan ~0.2 mA rata-rata (WiFi ON 5-10 detik per jam)
- Gateway: Tidak ada tambahan (WiFi sudah aktif)

### Q: Apakah OTA bisa rollback?

**A:**
Secara default, ESP32-OTA-Pull tidak support rollback. Namun, jika update gagal, device akan tetap menjalan firmware lama.

### Q: Bagaimana jika WiFi tidak ada?

**A:**
- Sensor nodes: Tidak ada OTA, tetap jalan normal via LoRa
- Gateway: Masuk Serial-Only mode, tetap jalan normal via LoRa ↔ Serial

### Q: Berapa lama proses OTA?

**A:**
- Check manifest: 2-5 detik
- Download firmware: 30-60 detik (tergantung ukuran & kecepatan WiFi)
- Install & reboot: 5-10 detik

### Q: Bisakah update beberapa device sekaligus?

**A:**
Ya, jika menggunakan `OTA_DEVICE_ID = ""` (kosong), semua device akan update.

---

## 📝 Catatan Penting

1. **Selalu backup firmware lama** sebelum update
2. **Test di device development** sebelum deploy ke production
3. **Monitor Serial Monitor** selama proses OTA
4. **Pastikan WiFi stabil** selama proses update
5. **Jangan matikan device** selama proses download/install

---

## 🔗 Referensi

- [ESP32-OTA-Pull GitHub](https://github.com/mikalhart/ESP32-OTA-Pull)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [WiFiManager Documentation](https://github.com/tzapu/WiFiManager)

---

*Last updated: July 14, 2026*
