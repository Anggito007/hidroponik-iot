/**
 * ============================================================
 * TEST SENSOR HC-SR04
 * ============================================================
 * Pengujian akurasi pembacaan jarak HC-SR04.
 * Bandingkan hasil dengan penggaris/meteran.
 *
 * WIRING:
 *   HC-SR04 VCC  → 5V ESP32
 *   HC-SR04 GND  → GND ESP32
 *   HC-SR04 TRIG → GPIO 32
 *   HC-SR04 ECHO → [Voltage Divider 1k/2k] → GPIO 33
 *
 * OUTPUT (Serial Monitor 115200 baud):
 *   No,Jarak(cm),Durasi(us),Status
 * ============================================================
 */

#define TRIG_PIN 32
#define ECHO_PIN 33
#define MAX_DIST 400  // cm

int counter = 0;

long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.println(F("============================================"));
  Serial.println(F("  TEST SENSOR HC-SR04"));
  Serial.println(F("  Pembacaan setiap 1 detik"));
  Serial.println(F("============================================"));
  Serial.println(F("No,Jarak(cm),Durasi(us),Status"));
}

void loop() {
  counter++;

  // Ambil 5 sample, ambil rata-rata
  long sum = 0; int valid = 0;
  long lastDur = 0;
  for (int i = 0; i < 5; i++) {
    long dur = readUltrasonic();
    if (dur > 0) {
      long cm = dur / 58;
      if (cm > 0 && cm <= MAX_DIST) {
        sum += cm;
        valid++;
        lastDur = dur;
      }
    }
    delay(60);
  }

  if (valid > 0) {
    long avg = sum / valid;
    Serial.printf("%d,%ld,%ld,OK\n", counter, avg, lastDur);
  } else {
    Serial.printf("%d,ERROR,0,Tidak ada pantulan\n", counter);
  }

  delay(1000);
}
