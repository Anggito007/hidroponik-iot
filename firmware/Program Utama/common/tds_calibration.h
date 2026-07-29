/**
 * ============================================================
 * TDS Calibration Module — HydroIoT  (REV 2 - piecewise linear)
 * ============================================================
 * Modul kalibrasi TDS (Total Dissolved Solids) untuk sensor
 * TDS Meter pada sistem hidroponik.
 *
 * PERUBAHAN PENTING (v2):
 *  Versi sebelumnya menghitung SATU garis regresi linear
 *  (TDS = m*V + b) dari SEMUA titik kalibrasi sekaligus.
 *  Karena hubungan tegangan->ppm sensor TDS sebenarnya
 *  melengkung (lihat rumus kubik fallback di bawah), satu
 *  garis lurus itu paling meleset justru di titik TENGAH
 *  rentang kalibrasi (mis. 600 & 900 ppm saat kalibrasi
 *  0-250-500-750-1000), walau titik ujungnya kelihatan pas.
 *
 *  Versi ini menggunakan INTERPOLASI LINEAR PER-SEGMEN
 *  (piecewise linear): setiap titik kalibrasi yang Anda
 *  simpan akan terbaca 100% pas, dan nilai di antara dua
 *  titik dihitung dari garis lokal antara keduanya saja
 *  (bukan dipaksa 1 garis untuk semua titik). Jika 600/900
 *  ppm masih kurang akurat, cukup tambah titik kalibrasi
 *  baru tepat di 600 dan 900 ppm.
 *
 * Fitur:
 *  - Pembacaan tegangan sensor TDS dengan kompensasi suhu
 *  - Konversi tegangan -> PPM via interpolasi linear piecewise
 *  - Kalibrasi multi-titik (hingga 10 titik) via NVS flash,
 *    otomatis terurut berdasarkan tegangan
 *  - Fallback ke rumus kubik bawaan jika belum dikalibrasi
 *    (atau baru 1 titik)
 *  - R2 garis regresi global tetap dihitung sebagai info
 *    linearitas sensor (BUKAN dipakai untuk konversi PPM)
 *
 * Penggunaan:
 *  1. Buat instance: TDSCal tds;
 *  2. Inisialisasi:  tds.begin(prefs);
 *  3. Baca PPM:      float ppm = tds.readPPM(pin, suhu);
 *  4. Kalibrasi:     tds.addCalPoint(pin, suhu, ppmReferensi);
 *
 * Penulis: Anggito Alif Abimanyu
 * ============================================================
 */

#ifndef TDS_CALIBRATION_H
#define TDS_CALIBRATION_H

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

class TDSCal {
public:
    // ============================================================
    //  KONFIGURASI (bisa diubah sebelum begin())
    // ============================================================

    /**
     * Jumlah maksimum titik kalibrasi.
     * Default: 10. Ubah sebelum pemanggilan begin() jika perlu.
     */
    int maxCalPoints = 10;

    /**
     * Jumlah sampel ADC per pembacaan.
     * Semakin tinggi, semakin halus (tapi lebih lambat).
     * Default: 10
     */
    int adcSamples = 10;

    /**
     * Tegangan referensi ADC ESP32 (Volt).
     * Default: 3.3V
     */
    float adcRefVoltage = 3.3;

    /**
     * Resolusi ADC ESP32 (12-bit = 4095).
     * Default: 4095
     */
    int adcResolution = 4095;

    /**
     * Koefisien kompensasi suhu (% per °C).
     * Konduktivitas larutan naik ~2% per °C.
     * Default: 0.02
     */
    float tempCoeff = 0.02;

    /**
     * Suhu referensi untuk normalisasi (°C).
     * Default: 25.0
     */
    float refTemp = 25.0;

    // ============================================================
    //  FUNGSI UTAMA
    // ============================================================

    /**
     * Inisialisasi modul dan load kalibrasi dari NVS flash.
     * Panggil sekali di setup().
     *
     * @param prefs  Instance Preferences dari ESP32 (akan dibuka dengan namespace "tdslr")
     */
    void begin(Preferences& prefs) {
        _prefs = &prefs;
        _load();
    }

    /**
     * Baca tegangan terkompensasi suhu dari sensor TDS.
     * Fungsi ini melakukan:
     *  1. Rata-rata ADC (adcSamples kali) untuk kurangi noise
     *  2. Konversi ADC → tegangan (Volt)
     *  3. Kompensasi suhu: normalisasi ke refTemp (25°C)
     *
     * @param pin     Pin ADC sensor TDS (misal: 39 atau 35)
     * @param tempC   Suhu saat ini dalam °C (gunakan 25.0 jika sensor suhu error)
     * @return        Tegangan terkompensasi (Volt)
     */
    float readVoltage(int pin, float tempC) {
        long adcSum = 0;
        for (int i = 0; i < adcSamples; i++) {
            adcSum += analogRead(pin);
            delay(2);
        }
        float adcAvg = (float)adcSum / adcSamples;

        // Konversi ADC → Tegangan (Volt)
        float voltage = adcAvg * adcRefVoltage / adcResolution;

        // Kompensasi suhu: normalisasi ke refTemp (25°C)
        float tempComp = 1.0 + tempCoeff * (tempC > -50 ? tempC - refTemp : 0);
        float compensatedVoltage = voltage / tempComp;

        return compensatedVoltage;
    }

    /**
     * Konversi tegangan ke TDS dalam PPM.
     * Jika sudah dikalibrasi (>= 2 titik): interpolasi linear
     *   piecewise antar titik kalibrasi terurut (akurat persis
     *   di setiap titik kalibrasi, bukan hanya mendekati).
     * Jika belum (< 2 titik): gunakan rumus kubik bawaan.
     *
     * @param voltage  Tegangan terkompensasi dari readVoltage()
     * @return         TDS dalam PPM (selalu >= 0)
     */
    float readPPM(float voltage) {
        if (!_valid) {
            // Fallback: rumus kubik bawaan
            float t = (133.42 * voltage * voltage * voltage
                     - 255.86 * voltage * voltage
                     + 857.39 * voltage) * 0.5;
            return t > 0 ? t : 0;
        }
        float tds = _interpolate(voltage);
        return tds > 0 ? tds : 0;
    }

    /**
     * Konversi tegangan ke TDS dengan pembacaan langsung dari pin.
     * Gabungan readVoltage() + readPPM().
     *
     * @param pin    Pin ADC sensor TDS
     * @param tempC  Suhu saat ini (°C)
     * @return       TDS dalam PPM
     */
    float readPPMFromPin(int pin, float tempC) {
        float voltage = readVoltage(pin, tempC);
        return readPPM(voltage);
    }

    // ============================================================
    //  KALIBRASI
    // ============================================================

    /**
     * Tambah titik kalibrasi baru.
     * Membaca tegangan saat ini, menyimpan pasangan (voltage, ppm)
     * secara TERURUT berdasarkan tegangan (dibutuhkan untuk
     * interpolasi piecewise), lalu menghitung ulang statistik.
     *
     * @param pin      Pin ADC sensor TDS
     * @param tempC    Suhu saat ini (°C)
     * @param refPPM   TDS referensi dari larutan kalibrasi (ppm)
     * @return         true jika berhasil, false jika gagal
     */
    bool addCalPoint(int pin, float tempC, float refPPM) {
        if (refPPM < 0) {
            Serial.println(F("[CAL] GAGAL: PPM harus >= 0"));
            return false;
        }

        float voltage = readVoltage(pin, tempC);

        if (voltage <= 0.001) {
            Serial.println(F("[CAL] GAGAL: Tegangan = 0, cek sensor!"));
            return false;
        }

        // Kalau tegangan hampir sama dengan titik yang sudah ada,
        // perbarui titik itu saja (hindari duplikat/pembagian nol
        // saat interpolasi)
        for (int i = 0; i < _count; i++) {
            if (fabs(_voltages[i] - voltage) < 0.001) {
                _refPPMs[i] = refPPM;
                Serial.printf("[CAL] Titik dengan V~%.4fV diperbarui -> Ref=%dppm\n",
                    voltage, (int)refPPM);
                _calcRegression();
                _save();
                return true;
            }
        }

        if (_count >= maxCalPoints) {
            Serial.printf("[CAL] GAGAL: Maksimal %d titik. Kirim reset untuk hapus.\n", maxCalPoints);
            return false;
        }

        // Cari posisi sisip agar array tetap terurut menaik
        // berdasarkan tegangan (dibutuhkan oleh _interpolate())
        int insertPos = _count;
        for (int i = 0; i < _count; i++) {
            if (voltage < _voltages[i]) { insertPos = i; break; }
        }
        for (int i = _count; i > insertPos; i--) {
            _voltages[i] = _voltages[i - 1];
            _refPPMs[i]  = _refPPMs[i - 1];
        }
        _voltages[insertPos] = voltage;
        _refPPMs[insertPos]  = refPPM;
        _count++;

        Serial.printf("[CAL] Titik %d: V=%.4fV → Ref=%dppm\n",
            _count, voltage, (int)refPPM);

        _calcRegression();
        _save();

        return true;
    }

    /**
     * Hapus semua data kalibrasi dari NVS dan reset koefisien.
     * Setelah reset, modul akan menggunakan rumus kubik bawaan.
     */
    void resetCalibration() {
        _count = 0;
        _m = 0;
        _b = 0;
        _r2 = 0;
        _valid = false;
        _save();
        Serial.println(F("[CAL] Reset! Semua data kalibrasi dihapus."));
    }

    // ============================================================
    //  GETTER
    // ============================================================

    /** Slope garis regresi GLOBAL (info linearitas saja, bukan dipakai untuk konversi) */
    float getSlope() const { return _m; }

    /** Intercept garis regresi GLOBAL (info linearitas saja, bukan dipakai untuk konversi) */
    float getIntercept() const { return _b; }

    /** R2 garis regresi GLOBAL: makin dekat ke 1 = sensor makin mendekati linear murni */
    float getR2() const { return _r2; }

    /** Jumlah titik kalibrasi yang tersimpan */
    int getCalCount() const { return _count; }

    /** Apakah kalibrasi aktif dipakai (minimal 2 titik tersimpan) */
    bool isValid() const { return _valid; }

    /** Tegangan per titik kalibrasi, terurut menaik (untuk debugging) */
    float getCalVoltage(int index) const {
        if (index >= 0 && index < _count) return _voltages[index];
        return 0;
    }

    /** PPM referensi per titik kalibrasi, terurut menaik (untuk debugging) */
    float getCalRefPPM(int index) const {
        if (index >= 0 && index < _count) return _refPPMs[index];
        return 0;
    }

    // ============================================================
    //  DEBUG
    // ============================================================

    /**
     * Cetak informasi kalibrasi ke Serial Monitor.
     * Panggil di setup() untuk verifikasi status kalibrasi.
     *
     * @param nodeId  ID node (untuk pesan debug)
     */
    void printInfo(int nodeId) {
        Serial.printf("  Kalibrasi TDS : Interpolasi Linear Piecewise\n");
        if (_valid) {
            Serial.printf("    Data titik  : %d/%d (terurut, tiap titik akurat persis)\n",
                _count, maxCalPoints);
            for (int i = 0; i < _count; i++) {
                Serial.printf("      [%d] V=%.4f → %dppm\n",
                    i + 1, _voltages[i], (int)_refPPMs[i]);
            }
            Serial.printf("    Info linearitas global: TDS ~= %.2f*V + %.2f | R2=%.4f\n",
                _m, _b, _r2);
        } else {
            Serial.printf("    Status      : BELUM DIKALIBRASI (pakai rumus kubik bawaan)\n");
            Serial.printf("    Titik data  : %d/%d (minimal 2 untuk interpolasi)\n",
                _count, maxCalPoints);
            Serial.printf("    Cara: CALADD:%d:<ppm> — celup sensor, kirim perintah\n",
                nodeId);
        }
    }

private:
    Preferences* _prefs = nullptr;

    // Data kalibrasi (SELALU terurut menaik berdasarkan _voltages)
    float _voltages[10];
    float _refPPMs[10];
    int   _count = 0;

    // Statistik regresi linear GLOBAL — hanya untuk info linearitas,
    // TIDAK dipakai untuk konversi PPM (lihat _interpolate())
    float _m    = 0;
    float _b    = 0;
    float _r2   = 0;
    bool  _valid = false;  // true jika >= 2 titik tersimpan

    /**
     * Interpolasi/ekstrapolasi linear piecewise berdasarkan
     * titik-titik kalibrasi terurut.
     *  - Di antara 2 titik  -> interpolasi linear segmen tsb
     *  - Di luar rentang    -> ekstrapolasi pakai slope segmen
     *                          terluar (ujung bawah/atas)
     */
    float _interpolate(float voltage) {
        int n = _count;

        if (voltage <= _voltages[0]) {
            float dv = _voltages[1] - _voltages[0];
            if (fabs(dv) < 1e-6) return _refPPMs[0];
            float slope = (_refPPMs[1] - _refPPMs[0]) / dv;
            return _refPPMs[0] + slope * (voltage - _voltages[0]);
        }

        if (voltage >= _voltages[n - 1]) {
            float dv = _voltages[n - 1] - _voltages[n - 2];
            if (fabs(dv) < 1e-6) return _refPPMs[n - 1];
            float slope = (_refPPMs[n - 1] - _refPPMs[n - 2]) / dv;
            return _refPPMs[n - 1] + slope * (voltage - _voltages[n - 1]);
        }

        for (int i = 0; i < n - 1; i++) {
            if (voltage >= _voltages[i] && voltage <= _voltages[i + 1]) {
                float dv = _voltages[i + 1] - _voltages[i];
                if (fabs(dv) < 1e-6) return _refPPMs[i];
                float frac = (voltage - _voltages[i]) / dv;
                return _refPPMs[i] + frac * (_refPPMs[i + 1] - _refPPMs[i]);
            }
        }
        return _refPPMs[n - 1]; // safety net, seharusnya tak pernah tercapai
    }

    void _load() {
        if (!_prefs) return;
        _prefs->begin("tdslr", true);
        _count = _prefs->getInt("n", 0);
        if (_count > maxCalPoints) _count = maxCalPoints;
        _m     = _prefs->getFloat("m", 0);
        _b     = _prefs->getFloat("b", 0);
        _r2    = _prefs->getFloat("r2", 0);
        _valid = _prefs->getBool("ok", false);
        for (int i = 0; i < _count; i++) {
            _voltages[i] = _prefs->getFloat(("v" + String(i)).c_str(), 0);
            _refPPMs[i]  = _prefs->getFloat(("p" + String(i)).c_str(), 0);
        }
        _prefs->end();
    }

    void _save() {
        if (!_prefs) return;
        _prefs->begin("tdslr", false);
        _prefs->putInt("n", _count);
        _prefs->putFloat("m", _m);
        _prefs->putFloat("b", _b);
        _prefs->putFloat("r2", _r2);
        _prefs->putBool("ok", _valid);
        for (int i = 0; i < _count; i++) {
            _prefs->putFloat(("v" + String(i)).c_str(), _voltages[i]);
            _prefs->putFloat(("p" + String(i)).c_str(), _refPPMs[i]);
        }
        _prefs->end();
    }

    /**
     * Hitung regresi linear GLOBAL: TDS = m x voltage + b
     * HANYA untuk info diagnostik (lihat printInfo/list), TIDAK
     * dipakai lagi untuk konversi PPM aktual — itu memakai
     * _interpolate() (piecewise linear per segmen).
     *
     * _valid diset true begitu jumlah titik >= 2, terlepas dari
     * hasil regresi global (supaya interpolasi tetap aktif walau
     * garis regresi globalnya degenerate).
     */
    void _calcRegression() {
        _valid = (_count >= 2);

        if (_count < 2) {
            _m = 0; _b = 0; _r2 = 0;
            return;
        }

        float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        int n = _count;

        for (int i = 0; i < n; i++) {
            float x = _voltages[i];
            float y = _refPPMs[i];
            sumX  += x;
            sumY  += y;
            sumXY += x * y;
            sumX2 += x * x;
        }

        float denom = (n * sumX2) - (sumX * sumX);
        if (fabs(denom) < 1e-9) {
            _m = 0; _b = 0; _r2 = 0;
            return;
        }

        _m = ((n * sumXY) - (sumX * sumY)) / denom;
        _b = (sumY - _m * sumX) / n;

        float meanY = sumY / n;
        float ssTot = 0, ssRes = 0;
        for (int i = 0; i < n; i++) {
            float predicted = _m * _voltages[i] + _b;
            ssRes += (_refPPMs[i] - predicted) * (_refPPMs[i] - predicted);
            ssTot += (_refPPMs[i] - meanY) * (_refPPMs[i] - meanY);
        }
        _r2 = (ssTot > 0) ? (1.0 - ssRes / ssTot) : 0;

        Serial.printf("[CAL] Interpolasi piecewise aktif (%d titik) | Info linearitas: TDS ~= %.2f*V + %.2f | R2=%.4f\n",
            n, _m, _b, _r2);
    }
};

#endif // TDS_CALIBRATION_H
