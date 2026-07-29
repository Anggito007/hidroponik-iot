/**
 * HydroIoT — TDS Calibration Tool
 * ============================================================
 * Program standalone untuk kalibrasi sensor TDS.
 *
 * Cara pakai:
 *   1. Upload sketch ini ke ESP32 Node.
 *   2. Buka Serial Monitor (baud 115200).
 *   3. Ikuti menu interaktif untuk kalibrasi.
 *   4. Setelah selesai, upload firmware Node utama.
 *
 * Data kalibrasi disimpan di NVS (Preferences) namespace "tdslr".
 * Format SAMA dengan modul tds_calibration.h yang digunakan
 * oleh firmware Node utama.
 *
 * GPIO yang digunakan (SAMA dengan Node 2):
 *   TDS_PIN = 39 (ADC1)
 *   DHT_PIN = 4  (untuk kompensasi suhu)
 *
 * Untuk Node 3: ubah #define TDS_PIN menjadi 35
 * ============================================================
 */

#include <DHT.h>
#include <Preferences.h>
#include "../common/tds_calibration.h"

#define DHT_PIN  4
#define TDS_PIN  39   // <- ubah ke 35 untuk Node 3

DHT dht(DHT_PIN, DHT22);
Preferences prefs;
TDSCal tds;

unsigned long lastDisplay = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n========================================"));
  Serial.println(F("  HydroIoT — TDS Calibration Tool"));
  Serial.println(F("========================================\n"));

  dht.begin();
  delay(2000);

  tds.begin(prefs);
  tds.printInfo(2);  // node ID untuk pesan debug

  printMenu();
}

void loop() {
  // Tampilkan pembacaan live setiap 2 detik
  if (millis() - lastDisplay >= 2000) {
    lastDisplay = millis();

    float temp = dht.readTemperature();
    if (isnan(temp)) temp = 25.0;
    float volt = tds.readVoltage(TDS_PIN, temp);
    float ppm  = tds.readPPM(volt);

    Serial.printf("[LIVE] Suhu:%.1fC | V:%.4fV | TDS:%d ppm | Kal:%d titik R2=%.4f\n",
      temp, volt, (int)ppm, tds.getCalCount(), tds.getR2());
  }

  // Terima perintah dari Serial Monitor
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;
    processCommand(cmd);
  }
}

void printMenu() {
  Serial.println(F("\n╔══════════════════════════════════════════╗"));
  Serial.println(F("║              MENU KALIBRASI              ║"));
  Serial.println(F("╠══════════════════════════════════════════╣"));
  Serial.println(F("║ add <ppm>   Tambah titik kalibrasi       ║"));
  Serial.println(F("║ list        Tampilkan semua titik        ║"));
  Serial.println(F("║ reset       Hapus semua kalibrasi        ║"));
  Serial.println(F("║ help        Tampilkan menu ini           ║"));
  Serial.println(F("╚══════════════════════════════════════════╝"));
  Serial.println(F("\n  Contoh (semakin banyak titik = semakin akurat,"));
  Serial.println(F("  terutama tambahkan titik PERSIS di ppm yang sering"));
  Serial.println(F("  Anda pakai, mis. 600 & 900):"));
  Serial.println(F("    add 0        (air murni)"));
  Serial.println(F("    add 250"));
  Serial.println(F("    add 500"));
  Serial.println(F("    add 600"));
  Serial.println(F("    add 750"));
  Serial.println(F("    add 900"));
  Serial.println(F("    add 1000\n"));

  Serial.println(F("  Cara kalibrasi:"));
  Serial.println(F("    1. Celupkan sensor ke larutan referensi"));
  Serial.println(F("    2. Tunggu pembacaan stabil (lihat [LIVE])"));
  Serial.println(F("    3. Ketik: add <ppm>"));
  Serial.println(F("    4. Ulangi untuk setiap titik"));
  Serial.println(F("    Catatan: konversi memakai interpolasi linear"));
  Serial.println(F("    antar titik, jadi setiap titik yang Anda"));
  Serial.println(F("    tambahkan akan terbaca akurat persis.\n"));
}

void processCommand(const String& cmd) {
  // ─── help ───
  if (cmd == "help") {
    printMenu();
    return;
  }

  // ─── list ───
  if (cmd == "list") {
    int n = tds.getCalCount();
    if (n == 0) {
      Serial.println(F("[CAL] Belum ada data kalibrasi."));
    } else {
      Serial.printf("[CAL] %d titik kalibrasi tersimpan:\n", n);
      for (int i = 0; i < n; i++) {
        Serial.printf("  [%d] V=%.4fV → %d ppm\n",
          i + 1, tds.getCalVoltage(i), (int)tds.getCalRefPPM(i));
      }
      if (tds.isValid()) {
        Serial.println(F("  Konversi aktif: interpolasi linear antar titik di atas"));
        Serial.printf("  (info linearitas sensor: TDS ~= %.4f x V + %.4f, R² = %.4f)\n",
          tds.getSlope(), tds.getIntercept(), tds.getR2());
      }
    }
    return;
  }

  // ─── add <ppm> ───
  if (cmd.startsWith("add ")) {
    const char* p = cmd.c_str() + 4;
    float targetPpm = atof(p);

    if (targetPpm < 0) {
      Serial.println(F("[CAL] ERROR: PPM harus >= 0"));
      return;
    }

    float temp = dht.readTemperature();
    if (isnan(temp)) temp = 25.0;

    Serial.printf("[CAL] Membaca sensor... (target: %.0f ppm)\n", targetPpm);

    if (tds.addCalPoint(TDS_PIN, temp, targetPpm)) {
      Serial.printf("[CAL] ✓ Titik %d/%d berhasil!\n", tds.getCalCount(), tds.maxCalPoints);
      if (tds.isValid()) {
        Serial.println(F("[CAL] Konversi memakai interpolasi linear antar titik (tiap titik akurat persis)."));
      }
    }
    return;
  }

  // ─── reset ───
  if (cmd == "reset") {
    Serial.println(F("[CAL] Menghapus semua data kalibrasi..."));
    tds.resetCalibration();
    Serial.println(F("[CAL] ✓ Kalibrasi direset. Sensor menggunakan rumus bawaan."));
    return;
  }

  Serial.printf("[?] Perintah tidak dikenal: '%s'. Ketik 'help' untuk menu.\n", cmd.c_str());
}
