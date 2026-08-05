/**
 * ============================================================================
 * PROGRAM KHUSUS PENGUJIAN JARAK DAN QoS LORA (NODE TEST SKETCH)
 * Project: Hidroponik IoT - Kangkung (LoRa E32-433T20D)
 * ============================================================================
 * Catatan: Program ini KHUSUS untuk pengujian independen.
 * TIDAK MENGANGGU kodingan di 'Program Utama'.
 *
 * Fitur:
 * - Pengiriman data LoRa P2P super cepat (non-blocking).
 * - Inisialisasi E32 Fixed Transmission Mode (0x00, GW_ADDL, LORA_CHAN).
 * - Pembacaan Sensor DHT22 (Suhu & Kelembapan) & Sensor TDS.
 * - Pengujian Kontrol Relay 1 (Pompa Nutrisi) & Relay 2 (Kipas).
 * - Support Tampilan LCD I2C (dengan deteksi aman agar tidak stuck).
 * - Menerima Perintah Serial dari PC untuk Uji Coba Relay (Tekan '1' / '2').
 * ============================================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── Ubah Alamat Node di Sini ───────────────────────────────
#define NODE_ADDL   0x02   // 0x02 untuk Node 2, 0x03 untuk Node 3
#define GW_ADDL     0x01   // Alamat Gateway
#define LORA_CHAN   0x17   // Frekuensi CH 23 (433 MHz)

// ─── Pin Definitions (ESP32 Node) ───────────────────────────
#define AUX_PIN     18
#define M0_PIN      21
#define M1_PIN      22
#define RX_PIN      16
#define TX_PIN      17
#define DHT_PIN      4
#define TDS_PIN     39
#define R1_PIN      25
#define R2_PIN      26
#define LCD_SDA     13
#define LCD_SCL     14

// ─── Interval Pengiriman Pengujian (2 Detik) ────────────────
#define SEND_INTERVAL 2000

// ─── Objek Perangkat ─────────────────────────────────────────
HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
DHT dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── Variabel Global ─────────────────────────────────────────
uint16_t seqNumber = 0;
unsigned long lastSendTime = 0;
bool relay1State = false;
bool relay2State = false;
bool lcdDetected = false;

float curTemp = 0.0, curHum = 0.0;
int curTds = 0;
float curVolt = 0.0;

// ─── Helper Konversi TDS ─────────────────────────────────────
float calcTDS(float voltage, float tempC) {
  float tempCoeff = 1.0f + 0.02f * (tempC > -50 ? tempC - 25.0f : 0);
  float compVolt = voltage / tempCoeff;
  float tds = (133.42f * compVolt * compVolt * compVolt 
             - 255.86f * compVolt * compVolt 
             + 857.39f * compVolt) * 0.5f;
  return tds > 0 ? tds : 0;
}

// ─── Helper Fungsi LoRa ──────────────────────────────────────
bool waitAuxReady(uint32_t timeoutMs = 2000) {
  uint32_t start = millis();
  while (digitalRead(AUX_PIN) == LOW) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  return true;
}

bool sendLoRaDirect(const char* payload) {
  if (!waitAuxReady(1500)) {
    // Clear buffer jika AUX stuck
    while (e32Serial.available()) e32Serial.read();
    e32Serial.flush();
    delay(50);
    if (!waitAuxReady(1000)) return false;
  }
  while (e32Serial.available()) e32Serial.read();

  // Header Fixed Transmission: ADDH (0x00) + ADDL (0x01) + CHAN (0x17)
  e32Serial.write((uint8_t)0x00);
  e32Serial.write((uint8_t)GW_ADDL);
  e32Serial.write((uint8_t)LORA_CHAN);
  e32Serial.write((const uint8_t*)payload, strlen(payload));
  e32Serial.flush();

  delay(30);
  return waitAuxReady(2000);
}

// ─── Pembacaan Sensor ────────────────────────────────────────
void sampleSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h)) curHum = h;
  if (!isnan(t)) curTemp = t;

  int rawAdc = analogRead(TDS_PIN);
  curVolt = (rawAdc / 4095.0f) * 3.3f;
  curTds = (int)calcTDS(curVolt, curTemp > 0 ? curTemp : 25.0f);
}

// ─── Update LCD (Aman / Non-Blocking) ────────────────────────
void updateLcd() {
  if (!lcdDetected) return;
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "N%d T:%.1f H:%.0f", NODE_ADDL, curTemp, curHum);
  snprintf(line2, sizeof(line2), "TDS:%d R:%d%d #%d", curTds, relay1State?1:0, relay2State?1:0, seqNumber);
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=================================================");
  Serial.printf("   TEST NODE %d — KHUSUS UJI JARAK & QoS LORA\n", NODE_ADDL);
  Serial.println("=================================================");

  // 1. Setup Relay Pins (Active LOW, Default OFF)
  pinMode(R1_PIN, OUTPUT); digitalWrite(R1_PIN, HIGH);
  pinMode(R2_PIN, OUTPUT); digitalWrite(R2_PIN, HIGH);
  Serial.println("[INIT] Relay Pins: OK (Default OFF)");

  // 2. Setup Mode Pin E32 (M0=0, M1=0 -> Normal Transmission Mode)
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  pinMode(AUX_PIN, INPUT);
  delay(100);

  // 3. Setup LCD (Deteksi I2C Aman)
  Wire.begin(LCD_SDA, LCD_SCL);
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcdDetected = true;
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0); lcd.print("TEST NODE LORA");
    lcd.setCursor(0, 1); lcd.print("Initializing...");
    Serial.println("[INIT] LCD I2C (0x27): Terdeteksi ✅");
  } else {
    Serial.println("[INIT] LCD I2C: Tidak terdeteksi / Tidak terpasang (Dipindah ke mode tanpa LCD)");
  }

  // 4. Setup DHT22 Sensor
  dht.begin();
  Serial.println("[INIT] DHT22 Sensor: OK");

  // 5. Setup LoRa E32 Serial
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();
  delay(200);

  // Konfigurasi E32 ke Fixed Transmission Mode
  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code == 1) {
    Configuration cfg = *(Configuration*)c.data;
    cfg.ADDH = 0x00;
    cfg.ADDL = NODE_ADDL;
    cfg.CHAN = LORA_CHAN;
    cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
    cfg.OPTION.fec = FEC_1_ON;
    cfg.OPTION.transmissionPower = POWER_20;
    cfg.SPED.airDataRate = AIR_DATA_RATE_010_24;
    e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
    c.close();
    Serial.println("[INIT] LoRa E32 Config: Fixed Mode 433MHz ✅");
  } else {
    Serial.println("[INIT] LoRa E32 Config: Menggunakan mode Direct Default");
  }

  Serial.println("\n[SYSTEM] Node Siap Memancar setiap 2 detik!");
  Serial.println("[HELP] Tekan '1' di Serial Monitor untuk Toggle Relay 1 (Pompa).");
  Serial.println("[HELP] Tekan '2' di Serial Monitor untuk Toggle Relay 2 (Kipas).\n");
}

// ─── Loop Utama ──────────────────────────────────────────────
void loop() {
  // 1. Cek Perintah Serial dari PC (Uji Coba Relay Manual)
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1') {
      relay1State = !relay1State;
      digitalWrite(R1_PIN, relay1State ? LOW : HIGH);
      Serial.printf("\n[TEST] Relay 1 Pompa %s\n", relay1State ? "ON 🟢" : "OFF 🔴");
    } else if (cmd == '2') {
      relay2State = !relay2State;
      digitalWrite(R2_PIN, relay2State ? LOW : HIGH);
      Serial.printf("\n[TEST] Relay 2 Kipas %s\n", relay2State ? "ON 🟢" : "OFF 🔴");
    }
  }

  // 2. Cek Perintah LoRa dari Gateway (CMD Saklar)
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      String rxMsg = rc.data;
      Serial.printf("\n[RX-GW] %s\n", rxMsg.c_str());
      if (rxMsg.indexOf("R1:1") >= 0) { relay1State = true; digitalWrite(R1_PIN, LOW); }
      if (rxMsg.indexOf("R1:0") >= 0) { relay1State = false; digitalWrite(R1_PIN, HIGH); }
      if (rxMsg.indexOf("R2:1") >= 0) { relay2State = true; digitalWrite(R2_PIN, LOW); }
      if (rxMsg.indexOf("R2:0") >= 0) { relay2State = false; digitalWrite(R2_PIN, HIGH); }
    }
  }

  // 3. Transmisi Periodik Data Pengujian
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();
    seqNumber++;

    sampleSensors();

    // Format Payload Pengujian: TEST:<node>:<seq>:<temp>:<hum>:<volt_mv>:<tds>:<r1>:<r2>:<millis>
    char payload[128];
    snprintf(payload, sizeof(payload),
      "TEST:%d:%u:%.1f:%.1f:%d:%d:%d:%d:%lu",
      NODE_ADDL, seqNumber, curTemp, curHum,
      (int)(curVolt * 1000), curTds,
      relay1State ? 1 : 0,
      relay2State ? 1 : 0,
      millis());

    bool ok = sendLoRaDirect(payload);
    Serial.printf("[TX #%u] %s ➔ %s\n", seqNumber, payload, ok ? "BERHASIL ✅" : "GAGAL ❌");

    updateLcd();
  }
}
