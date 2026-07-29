/**
 * ============================================================
 * HydroIoT — OTA Handler
 * ============================================================
 * Modul OTA (Over-The-Air) Update menggunakan ESP32-OTA-Pull
 * 
 * Strategi: WiFi Always ON (Gateway sudah WiFi aktif)
 *   - WiFi selalu aktif (menggunakan WiFiManager)
 *   - OTA check berjalan di background
 * 
 * Library: mikalhart/ESP32-OTA-Pull
 * Dependency: ArduinoJson
 * ============================================================
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32OTAPull.h>
#include "ota_config.h"

// ===== OTA STATE =====
enum OTAState {
  OTA_IDLE,           // Menunggu waktu check
  OTA_CONNECTING,     // Menghubungkan WiFi (skip untuk gateway)
  OTA_CHECKING,       // Mengecek update
  OTA_DOWNLOADING,    // Download firmware
  OTA_INSTALLING,     // Install firmware
  OTA_COMPLETE,       // Selesai, menunggu reboot
  OTA_ERROR           // Error, kembali ke normal
};

class OTAHandler {
private:
  ESP32OTAPull ota;
  OTAState state;
  unsigned long lastCheckTime;
  unsigned long stateStartTime;
  bool otaEnabled;
  
  // Callback progress
  static void progressCallback(int offset, int totallength) {
    if (OTA_DEBUG) {
      Serial.printf("[OTA] Progress: %d/%d (%d%%)\n", 
        offset, totallength, (offset * 100) / totallength);
    }
  }

  // LED blink saat OTA active
  void ledBlink(int times, int delayMs) {
    if (OTA_LED_PIN == 0) return;
    
    pinMode(OTA_LED_PIN, OUTPUT);
    for (int i = 0; i < times; i++) {
      digitalWrite(OTA_LED_PIN, HIGH);
      delay(delayMs / 2);
      digitalWrite(OTA_LED_PIN, LOW);
      delay(delayMs / 2);
    }
  }

public:
  OTAHandler() {
    state = OTA_IDLE;
    lastCheckTime = 0;
    stateStartTime = 0;
    otaEnabled = true;
  }

  // ===== PUBLIC METHODS =====

  // Inisialisasi OTA
  void begin() {
    if (OTA_DEBUG) {
      Serial.println(F("========================================"));
      Serial.println(F("  HydroIoT Gateway OTA System"));
      Serial.println(F("========================================"));
      Serial.printf("  Firmware    : %s\n", FIRMWARE_VERSION);
      Serial.printf("  Manifest    : %s\n", OTA_MANIFEST_URL);
      Serial.printf("  Interval    : %lu menit\n", OTA_CHECK_INTERVAL / 60000);
      Serial.printf("  Device ID   : %s\n", strlen(OTA_DEVICE_ID) > 0 ? OTA_DEVICE_ID : "(auto)");
      Serial.println(F("========================================\n"));
    }
    
    // Set callback progress
    ota.SetCallback(progressCallback);
    
    // Set device ID jika dikonfigurasi
    if (strlen(OTA_DEVICE_ID) > 0) {
      ota.OverrideDevice(OTA_DEVICE_ID);
    }
    
    // Set allow downgrade
    if (OTA_ALLOW_DOWNGRADE) {
      ota.AllowDowngrades(true);
    }
    
    // Reset timer untuk delay awal
    lastCheckTime = millis();
    
    if (OTA_DEBUG) Serial.println("[OTA] OTA Handler siap");
  }

  // Non-blocking update check - panggil di loop()
  void update() {
    if (!otaEnabled) return;
    
    unsigned long now = millis();
    
    switch (state) {
      case OTA_IDLE:
        // Cek apakah waktunya check update
        if (now - lastCheckTime >= OTA_CHECK_INTERVAL) {
          // Cek delay awal setelah boot
          if (now >= OTA_INITIAL_DELAY) {
            state = OTA_CHECKING;  // Langsung ke CHECKING (WiFi sudah ON)
            stateStartTime = now;
            if (OTA_DEBUG) Serial.println("\n[OTA] === Memulai pengecekan update ===");
          }
        }
        break;
        
      case OTA_CHECKING: {
        // Cek timeout
        if (now - stateStartTime > OTA_TIMEOUT) {
          Serial.println("[OTA] TIMEOUT saat cek update");
          state = OTA_IDLE;
          lastCheckTime = now;
          break;
        }
        
        if (OTA_DEBUG) Serial.println("[OTA] Mengecek update...");
        
        int result = ota.CheckForOTAUpdate(OTA_MANIFEST_URL, FIRMWARE_VERSION);
        
        switch (result) {
          case ESP32OTAPull::UPDATE_OK:
            // Update berhasil, device akan reboot
            Serial.println("[OTA] ✅ Update berhasil! Reboot dalam 3 detik...");
            ledBlink(5, 200);
            delay(3000);
            ESP.restart();  // Tidak akan sampai sini
            break;
            
          case ESP32OTAPull::NO_UPDATE_AVAILABLE:
            Serial.println("[OTA] ✅ Firmware sudah versi terbaru");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
            Serial.println("[OTA] ⚠️ Tidak ada profile yang cocok untuk device ini");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::UPDATE_AVAILABLE:
            Serial.println("[OTA] ℹ️ Update tersedia (mode: tidak auto-update)");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::HTTP_FAILED:
            Serial.println("[OTA] ❌ Error koneksi HTTP/Server");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::JSON_PROBLEM:
            Serial.println("[OTA] ❌ Manifest JSON tidak valid");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::WRITE_ERROR:
            Serial.println("[OTA] ❌ Error menulis firmware ke flash");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          case ESP32OTAPull::OTA_UPDATE_FAIL:
            Serial.println("[OTA] ❌ Proses update gagal");
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
            
          default:
            if (result > 0) {
              // HTTP status code (e.g., 404, 500)
              Serial.printf("[OTA] ❌ HTTP error: %d\n", result);
            } else {
              Serial.printf("[OTA] ⚠️ Unknown code: %d\n", result);
            }
            state = OTA_IDLE;
            lastCheckTime = now;
            break;
        }
        break;
      }
        
      case OTA_ERROR:
        // Tunggu sebentar sebelum retry
        if (now - stateStartTime > 5000) {
          state = OTA_IDLE;
          lastCheckTime = now;
        }
        break;
        
      default:
        state = OTA_IDLE;
        break;
    }
  }

  // Force check update (non-blocking)
  void forceCheck() {
    if (OTA_DEBUG) Serial.println("[OTA] Force check diminta");
    lastCheckTime = 0;  // Reset timer agar segera check
  }

  // Enable/Disable OTA
  void setEnabled(bool enabled) {
    otaEnabled = enabled;
    if (!enabled) {
      state = OTA_IDLE;
    }
  }

  // Get current state
  OTAState getState() {
    return state;
  }

  // Get state name
  const char* getStateName() {
    switch (state) {
      case OTA_IDLE:        return "IDLE";
      case OTA_CONNECTING:  return "CONNECTING";
      case OTA_CHECKING:    return "CHECKING";
      case OTA_DOWNLOADING: return "DOWNLOADING";
      case OTA_INSTALLING:  return "INSTALLING";
      case OTA_COMPLETE:    return "COMPLETE";
      case OTA_ERROR:       return "ERROR";
      default:              return "UNKNOWN";
    }
  }

  // Get firmware version
  const char* getVersion() {
    return FIRMWARE_VERSION;
  }
};

#endif // OTA_HANDLER_H
