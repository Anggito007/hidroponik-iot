/**
 * ============================================================
 * HydroIoT — OTA Configuration
 * ============================================================
 * Konfigurasi untuk ESP32-OTA-Pull (mikalhart/ESP32-OTA-Pull)
 * 
 * WiFi Strategy: Always ON (Gateway sudah WiFi aktif)
 * ============================================================
 */

#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

// ===== FIRMWARE VERSION =====
// Update versi ini setiap kali compile firmware baru
// Format: "major.minor.patch" (semver)
#define FIRMWARE_VERSION    "2.1.0"

// ===== DEVICE IDENTIFICATION =====
// Device ID untuk targeting di manifest JSON
// Kosongkan "" untuk update semua device
#define OTA_DEVICE_ID       ""          // Contoh: "node2", "node3", "gateway"

// ===== OTA MANIFEST URL =====
// URL ke file JSON manifest yang berisi info firmware terbaru
// HARUS HTTPS untuk keamanan
#define OTA_MANIFEST_URL    "https://example.com/firmware/manifest.json"

// ===== OTA CHECK INTERVAL =====
// Interval pengecekan update (dalam milidetik)
// Default: 1 jam = 3600000 ms
// Minimum: 5 menit = 300000 ms (jangan terlalu sering)
#define OTA_CHECK_INTERVAL  3600000     // 1 jam

// ===== WIFI CREDENTIALS =====
// Gateway uses WiFiManager, so these are not used
// but kept for compatibility with OTA handler
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"

// ===== WIFI TIMEOUT =====
// Gateway sudah WiFi ON, timeout tidak dipakai
#define WIFI_TIMEOUT        5000        // 5 detik (backup)

// ===== OTA BEHAVIOR =====
// Delay awal setelah boot sebelum pertama kali cek OTA (ms)
// Tujuan: biarkan sistem stabil dulu
#define OTA_INITIAL_DELAY   60000       // 60 detik (gateway lebih lama)

// Timeout total OTA process (ms)
// Jika melebihi timeout, batal dan lanjut normal
#define OTA_TIMEOUT         120000      // 2 menit

// Allow firmware downgrade (false = hanya update ke versi lebih tinggi)
#define OTA_ALLOW_DOWNGRADE false

// ===== PARTITION SCHEME =====
// PENTING: Di Arduino IDE, pilih partition scheme yang support OTA:
//   - "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
//   - "Huge APP (3MB No OTA/1MB SPIFFS)" - TIDAK SUPPORT OTA!
//   - "Minimal SPIFFS (1.3MB APP/700KB SPIFFS)"
// 
// Atau buat custom partition table dengan 2 APP partition

// ===== SERIAL OUTPUT =====
// Set true untuk debug OTA di Serial Monitor
#define OTA_DEBUG           true

// ===== LED INDICATOR (Optional) =====
// Set GPIO LED untuk indikasi OTA activity (0 = disabled)
// LED berkedip saat OTA check sedang berlangsung
#define OTA_LED_PIN         0           // 0 = disabled

#endif // OTA_CONFIG_H
