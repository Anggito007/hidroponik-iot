/**
 * ============================================================
 * TEST SENSOR TDS
 * ============================================================
 * Pengujian akurasi pembacaan TDS sensor.
 * Bandingkan hasil dengan TDS meter digital referensi.
 *
 * WIRING:
 *   TDS VCC  → 3.3V ESP32
 *   TDS GND  → GND ESP32
 *   TDS AOUT → GPIO 39 (VN)
 *   DHT22 (opsional, untuk kompensasi suhu)
 *
 * KALIBRASI: ketik "cal <ppm>" di Serial Monitor
 *   Contoh: cal 500  → kalibrasi ke larutan 500 ppm
 *
 * OUTPUT (Serial Monitor 115200 baud):
 *   No,ADC,Voltage(V),TDS_Raw(ppm),TDS_Cal(ppm),K_Factor,Status
 * ============================================================
 */

#include <DHT.h>
#include <Preferences.h>

#define TDS_PIN     39
#define DHT_PIN     4
#define ADC_SAMPLES 15

DHT dht(DHT_PIN, DHT22);
Preferences prefs;

float kFactor = 1.0;
int counter = 0;

int readAvg(int pin) {
  long s = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) { s += analogRead(pin); delay(5); }
  return s / ADC_SAMPLES;
}

float readTDSRaw(float temp) {
  int adc = readAvg(TDS_PIN);
  float v  = adc / 4095.0 * 3.3;
  float cv = v / (1.0 + 0.02 * (temp > -50 ? temp - 25.0 : 0));
  float t  = (133.42*cv*cv*cv - 255.86*cv*cv + 857.39*cv) * 0.5;
  return t > 0 ? t : 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();
  delay(2000);

  prefs.begin("tdstest", true);
  kFactor = prefs.getFloat("k", 1.0);
  prefs.end();

  Serial.println(F("============================================"));
  Serial.println(F("  TEST SENSOR TDS"));
  Serial.println(F("  Pembacaan setiap 2 detik"));
  Serial.printf("  K-factor tersimpan: %.4f\n", kFactor);
  Serial.println(F("  Kalibrasi: ketik 'cal <ppm>'"));
  Serial.println(F("============================================"));
  Serial.println(F("No,ADC,Voltage(V),TDS_Raw(ppm),TDS_Cal(ppm),K_Factor,Status"));
}

void loop() {
  // Cek input kalibrasi
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("cal ")) {
      float ppm = cmd.substring(4).toFloat();
      if (ppm > 0) {
        float temp = dht.readTemperature();
        float raw = readTDSRaw(temp);
        if (raw > 0) {
          kFactor = ppm / raw;
          prefs.begin("tdstest", false);
          prefs.putFloat("k", kFactor);
          prefs.end();
          Serial.printf(">> KALIBRASI OK! K-factor = %.4f (target %.0f ppm)\n", kFactor, ppm);
        } else {
          Serial.println(">> Kalibrasi GAGAL: raw = 0, cek sensor");
        }
      }
    }
  }

  counter++;
  float temp = dht.readTemperature();
  if (isnan(temp)) temp = 25.0;

  int adc = readAvg(TDS_PIN);
  float v = adc / 4095.0 * 3.3;
  float tdsRaw = readTDSRaw(temp);
  float tdsCal = tdsRaw * kFactor;

  Serial.printf("%d,%d,%.3f,%.0f,%.0f,%.4f,OK\n",
    counter, adc, v, tdsRaw, tdsCal, kFactor);

  delay(2000);
}
