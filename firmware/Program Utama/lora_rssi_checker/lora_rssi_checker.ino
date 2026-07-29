/**
 * HydroIoT — LoRa E32 RSSI Capability Checker (RAW BYTE version)
 * ============================================================
 * Program utilitas standalone untuk mendeteksi apakah modul LoRa
 * E32-433T20D Anda mendukung dan bisa mengaktifkan fitur RSSI.
 *
 * Versi ini menggunakan RAW MEMORY CASTING sehingga kompatibel
 * dengan versi library LoRa_E32 lama maupun baru (bebas error kompilasi).
 *
 * Pinout yang digunakan (SAMA dengan Node 2 & 3):
 *   AUX_PIN = 18
 *   M0_PIN  = 21
 *   M1_PIN  = 22
 *   RX_PIN  = 16 (ESP32 RX2 -> TXD LoRa)
 *   TX_PIN  = 17 (ESP32 TX2 -> RXD LoRa)
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>

// ─── Pin Definitions (WROVER-Safe Pins) ─────────────────────
// INFO: ESP32-WROVER menggunakan GPIO 16 dan 17 secara internal
// untuk PSRAM. Jangan gunakan GPIO 16/17 karena akan menyebabkan crash.
// Silakan ubah angka pin di bawah ini sesuai kabel fisik Anda:
#define RX_PIN    26   // Hubungkan ke TXD LoRa
#define TX_PIN    27   // Hubungkan ke RXD LoRa
#define AUX_PIN   25   // Hubungkan ke AUX LoRa
#define M0_PIN    32   // Hubungkan ke M0 LoRa
#define M1_PIN    33   // Hubungkan ke M1 LoRa

HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println(F("\n============================================="));
  Serial.println(F("    E32 LORA RSSI CAPABILITY CHECKER tool    "));
  Serial.println(F("=============================================\n"));

  // Inisialisasi UART ke LoRa E32
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  delay(200);

  // ──────────────────────────────────────────────────────────
  // TES 1: Baca Informasi Firmware Modul (Raw Memory)
  // ──────────────────────────────────────────────────────────
  Serial.println(F("[TES 1] Membaca Versi Firmware Modul..."));
  ResponseStructContainer infoContainer = e32.getModuleInformation();
  
  if (infoContainer.status.code == 1) {
    // Cast struct ke byte array untuk bypass perbedaan versi library
    byte* rawInfo = (byte*)infoContainer.data;
    
    // Struktur ModuleInformation: Byte 0=Header (0xC3), Byte 1=Model, Byte 2=Version, Byte 3=Features
    byte modelCode = rawInfo[1];
    byte version   = rawInfo[2];
    byte features  = rawInfo[3];

    Serial.printf("  -> Model Hardware (HEX) : %02X\n", modelCode);
    Serial.printf("  -> Versi Firmware (HEX) : %02X\n", version);
    Serial.printf("  -> Kode Fitur     (HEX) : %02X\n", features);
    
    // Versi 0x13 (v1.3) ke atas mendukung RSSI
    if (version >= 0x13) {
      Serial.println(F("  [HASIL] Firmware versi >= 1.3. Berpotensi besar mendukung RSSI."));
    } else {
      Serial.println(F("  [HASIL] Firmware versi lama (< 1.3). Kemungkinan besar TIDAK mendukung RSSI."));
    }
  } else {
    Serial.println(F("  [GAGAL] Tidak dapat berkomunikasi dengan LoRa E32. Cek kabel TX/RX/Power!"));
  }
  infoContainer.close();
  Serial.println();

  delay(500);

  // ──────────────────────────────────────────────────────────
  // TES 2: Uji Tulis & Baca Register RSSI (Raw Option Byte)
  // ──────────────────────────────────────────────────────────
  Serial.println(F("[TES 2] Menguji Aktivasi Register RSSI..."));
  
  // 1. Baca konfigurasi saat ini
  ResponseStructContainer configContainer = e32.getConfiguration();
  if (configContainer.status.code == 1) {
    Configuration* cfg = (Configuration*)configContainer.data;
    
    // Cast ke byte array. 
    // Layout memori Configuration: [0]ADDH, [1]ADDL, [2]SPED, [3]CHAN, [4]OPTION
    byte* cfgBytes = (byte*)cfg;
    byte originalOption = cfgBytes[4]; // Byte ke-5 adalah register OPTION
    
    // Pada E32, Bit 1 (nilai 0x02) pada register OPTION adalah Bit Aktif RSSI
    byte originalRssiBit = (originalOption & 0x02) >> 1;
    Serial.printf("  -> Status awal RSSI di register: %s (Bit OPTION = %02X)\n", 
      originalRssiBit == 1 ? "AKTIF" : "NON-AKTIF", originalOption);

    // 2. Coba nyalakan RSSI dengan operasi OR (nyalakan Bit 1)
    cfgBytes[4] |= 0x02; 
    
    Serial.printf("  -> Mencoba menulis konfigurasi baru (Bit OPTION = %02X)...\n", cfgBytes[4]);
    ResponseStatus ws = e32.setConfiguration(*cfg, WRITE_CFG_PWR_DWN_SAVE);
    if (ws.code == 1) {
      Serial.println(F("  -> Konfigurasi tertulis. Memverifikasi ulang..."));
      
      // 3. Baca ulang untuk verifikasi
      ResponseStructContainer verifyContainer = e32.getConfiguration();
      if (verifyContainer.status.code == 1) {
        Configuration* verifyCfg = (Configuration*)verifyContainer.data;
        byte* verifyBytes = (byte*)verifyCfg;
        byte verifiedOption = verifyBytes[4];
        byte verifiedRssiBit = (verifiedOption & 0x02) >> 1;
        
        Serial.printf("  -> Status setelah verifikasi: %s (Bit OPTION = %02X)\n", 
          verifiedRssiBit == 1 ? "AKTIF" : "NON-AKTIF", verifiedOption);

        Serial.println(F("\n================ KESIMPULAN ================"));
        if (verifiedRssiBit == 1) {
          Serial.println(F(" KEPUTUSAN: MODUL LORA ANDA MENDUKUNG FITUR RSSI! ✅"));
          Serial.println(F(" Bit konfigurasi RSSI berhasil disimpan secara permanen."));
        } else {
          Serial.println(F(" KEPUTUSAN: MODUL LORA ANDA TIDAK MENDUKUNG RSSI. ❌"));
          Serial.println(F(" Modul menolak penulisan bit RSSI dan meresetnya ke 0."));
        }
        Serial.println(F("============================================"));

        // Kembalikan konfigurasi ke status awal agar tidak mengacaukan program utama
        if (verifiedOption != originalOption) {
          Serial.println(F("\n[INFO] Mengembalikan konfigurasi E32 ke status awal..."));
          verifyBytes[4] = originalOption;
          e32.setConfiguration(*verifyCfg, WRITE_CFG_PWR_DWN_SAVE);
          Serial.println(F("[INFO] Selesai. Modul aman untuk digunakan kembali."));
        }

      } else {
        Serial.println(F("  [GAGAL] Verifikasi gagal. Koneksi terputus saat membaca ulang."));
      }
      verifyContainer.close();

    } else {
      Serial.println(F("  [GAGAL] Modul E32 menolak perintah penulisan konfigurasi."));
    }

  } else {
    Serial.println(F("  [GAGAL] Tidak dapat membaca konfigurasi saat ini."));
  }
  configContainer.close();
  
  Serial.println(F("\n--- Tes Selesai ---"));
}

void loop() {
  // Diam
}
