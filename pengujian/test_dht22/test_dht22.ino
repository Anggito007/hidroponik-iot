/**
 * ============================================================
 * TEST SENSOR DHT22
 * ============================================================
 * Pengujian akurasi pembacaan suhu dan kelembapan DHT22.
 * Bandingkan hasil dengan termometer/hygrometer referensi.
 *
 * WIRING:
 *   DHT22 VCC  → 3.3V ESP32
 *   DHT22 GND  → GND ESP32
 *   DHT22 DATA → GPIO 4
 *
 * OUTPUT (Serial Monitor 115200 baud):
 *   No,Suhu(°C),Kelembapan(%),Status
 * ============================================================
 */

#include <DHT.h>

#define DHT_PIN  4
#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);

int counter = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(DHT_PIN, INPUT_PULLUP);
  dht.begin();
  delay(2000);

  Serial.println(F("============================================"));
  Serial.println(F("  TEST SENSOR DHT22"));
  Serial.println(F("  Pembacaan setiap 2 detik"));
  Serial.println(F("============================================"));
  Serial.println(F("No,Suhu(°C),Kelembapan(%),Status"));
}

void loop() {
  counter++;

  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.printf("%d,ERROR,ERROR,GAGAL BACA\n", counter);
  } else {
    Serial.printf("%d,%.1f,%.1f,OK\n", counter, temp, hum);
  }

  delay(2000);
}
