/**
 * ============================================================================
 * PROGRAM KHUSUS PENGUJIAN COLLISION (TABRAKAN SINYAL LORA) — NODE SKETCH
 * Project: Hidroponik IoT - Kangkung (LoRa E32-433T20D)
 * ============================================================================
 * Catatan: Program ini KHUSUS untuk Pengujian Tabrakan Sinyal (Collision).
 * 100% TERISOLASI di folder baru, TIDAK MENGANGGU 'Program Utama'.
 *
 * Fitur Utama Pengujian Sesuai Skenario:
 * - Dukungan Dua Variasi Lead Node (Node 2 Lead ATAU Node 3 Lead).
 * - Master Sync Trigger dari Gateway: Menerima perintah TRIG dari Gateway via LoRa.
 *   Menjamin pengiriman simultan skala mikrodetik (bebas human error).
 * - Pengiriman tepat 100 Paket per Batch (Batch 1/100 s.d. 100/100).
 * - Otomatis BERHENTI setelah 100 paket dikirim (Auto-Stop).
 * - Kelipatan Offset Waktu (0ms, 5ms, 10ms, 15ms ... 50ms) diatur otomatis.
 * ============================================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── Ubah ID Node di Sini ───────────────────────────────────
#define NODE_ADDL   0x03   // 0x02 untuk Node 2, 0x03 untuk Node 3
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
#define BUTTON_PIN   0   // Tombol BOOT bawaan ESP32

// ─── Konfigurasi Pengujian Collision ───────────────────────
#define MAX_BATCH_PACKETS 100     // Berhenti otomatis setiap 100 paket
#define BASE_INTERVAL_MS  5000    // Interval dasar 5 detik

// ─── Objek Perangkat ─────────────────────────────────────────
HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
DHT dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── Variabel Pengujian ──────────────────────────────────────
uint16_t batchSeq = 0;            // Nomor urut dalam batch (1 s.d. 100)
uint16_t batchNumber = 0;         // Nomor batch pengujian
int offsetMs = 0;                 // Kelipatan offset (0ms s.d. 50ms)
int leadNodeId = 2;               // Node yang jadi starter duluan (2 atau 3)
unsigned long lastSendTime = 0;
bool isBatchRunning = false;      // Default: diam menunggu perintah TRIG dari Gateway
bool lcdDetected = false;

float curTemp = 26.5, curHum = 75.0;
int curTds = 850;

// ─── Helper LoRa TX ──────────────────────────────────────────
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
    while (e32Serial.available()) e32Serial.read();
    e32Serial.flush();
    delay(50);
    if (!waitAuxReady(1000)) return false;
  }
  while (e32Serial.available()) e32Serial.read();

  // Header Fixed Transmission
  e32Serial.write((uint8_t)0x00);
  e32Serial.write((uint8_t)GW_ADDL);
  e32Serial.write((uint8_t)LORA_CHAN);
  e32Serial.write((const uint8_t*)payload, strlen(payload));
  e32Serial.flush();

  delay(30);
  return waitAuxReady(2000);
}

// ─── Update Tampilan LCD ─────────────────────────────────────
void updateLcdCollision() {
  if (!lcdDetected) return;
  char l1[17], l2[17];
  if (isBatchRunning) {
    snprintf(l1, sizeof(l1), "N%d (Lead:N%d)", NODE_ADDL, leadNodeId);
    snprintf(l2, sizeof(l2), "%3d/100 +%dms", batchSeq, offsetMs);
  } else {
    snprintf(l1, sizeof(l1), "N%d STANDBY #%d", NODE_ADDL, batchNumber);
    snprintf(l2, sizeof(l2), "Wait GW Trigger");
  }
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

// ─── Mulai Batch Pengujian Baru (Sync) ───────────────────────
void startBatchSync(int leadNode, int newOffsetMs) {
  batchSeq = 0;
  batchNumber++;
  isBatchRunning = true;
  leadNodeId = leadNode;
  
  // Jika node ini adalah Lead Node -> offsetMs = 0 (memancar t=0ms)
  // Jika node ini adalah Follower Node -> offsetMs = newOffsetMs
  if (NODE_ADDL == leadNode) {
    offsetMs = 0;
  } else {
    offsetMs = newOffsetMs;
  }

  Serial.println("\n=================================================");
  Serial.printf("🚀 SINKRONISASI BATCH #%d (Lead: Node %d | Target: 100 Pkt | Offset: +%dms)\n",
    batchNumber, leadNode, offsetMs);
  Serial.println("=================================================");

  // Tunda pengiriman pertama sesuai offset skala mikrodetik jika ini follower node
  if (offsetMs > 0) {
    delayMicroseconds((uint32_t)offsetMs * 1000);
  }

  lastSendTime = millis();
  updateLcdCollision();
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=================================================");
  Serial.printf("   COLLISION TEST NODE %d (VARIASE LEAD NODE)\n", NODE_ADDL);
  Serial.println("=================================================");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(R1_PIN, OUTPUT); digitalWrite(R1_PIN, HIGH);
  pinMode(R2_PIN, OUTPUT); digitalWrite(R2_PIN, HIGH);

  // Setup LoRa Pin Mode 0 (Normal)
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  pinMode(AUX_PIN, INPUT);

  // Setup LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcdDetected = true;
    lcd.init(); lcd.backlight();
    Serial.println("[INIT] LCD I2C Terdeteksi ✅");
  }

  // Setup LoRa E32
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();
  delay(200);

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code == 1) {
    Configuration cfg = *(Configuration*)c.data;
    cfg.ADDH = 0x00; cfg.ADDL = NODE_ADDL; cfg.CHAN = LORA_CHAN;
    cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
    cfg.OPTION.fec = FEC_1_ON;
    cfg.OPTION.transmissionPower = POWER_20;
    cfg.SPED.airDataRate = AIR_DATA_RATE_010_24;
    e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
    c.close();
    Serial.println("[INIT] LoRa E32 Configured: Fixed 433MHz ✅");
  }

  Serial.println("\n[SYSTEM] Node Siap & STANDBY.");
  Serial.println(" 🔹 Ketik 'trig 10' di Gateway  ➔ Node 2 Mulai Duluan (Node 3 Offset +10ms)");
  Serial.println(" 🔹 Ketik 'trig3 10' di Gateway ➔ Node 3 Mulai Duluan (Node 2 Offset +10ms)\n");

  updateLcdCollision();
}

// ─── Loop Utama ──────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // 1. Terima Perintah TRIG dari Gateway via LoRa (Master Sync Trigger)
  // Format 1: TRIG:<lead_node>:<offset_ms>  (misal: TRIG:2:10 atau TRIG:3:10)
  // Format 2: TRIG:<offset_ms>              (default lead_node = 2)
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      if (msg.startsWith("TRIG:")) {
        String body = msg.substring(5);
        int colonIdx = body.indexOf(':');
        int leadNode = 2;
        int offVal = 0;
        if (colonIdx > 0) {
          leadNode = body.substring(0, colonIdx).toInt();
          offVal = body.substring(colonIdx + 1).toInt();
        } else {
          offVal = body.toInt();
        }
        startBatchSync(leadNode, offVal);
      }
    }
  }

  // 2. Transmisi Paket Pengujian Collision (Setiap 5 Detik)
  if (isBatchRunning) {
    uint32_t targetInterval = BASE_INTERVAL_MS;

    if (now - lastSendTime >= targetInterval) {
      lastSendTime = now;
      batchSeq++;

      // Format Payload: COL:<node>:<batch_seq>:<offset_ms>:<temp>:<hum>:<tds>:<millis>
      char payload[128];
      snprintf(payload, sizeof(payload),
        "COL:%d:%u:%d:%.1f:%.1f:%d:%lu",
        NODE_ADDL, batchSeq, offsetMs, curTemp, curHum, curTds, millis());

      bool ok = sendLoRaDirect(payload);
      Serial.printf("[COL-TX N%d] Paket %3d/100 (Lead:N%d | Offset +%dms) ➔ %s\n",
        NODE_ADDL, batchSeq, leadNodeId, offsetMs, ok ? "BERHASIL ✅" : "GAGAL ❌");

      updateLcdCollision();

      // ── CEK AUTO-STOP JIKA SUDAH MENCAPAI 100 PAKET ──
      if (batchSeq >= MAX_BATCH_PACKETS) {
        isBatchRunning = false;
        Serial.println("\n=================================================");
        Serial.printf("🛑 BATCH #%d SELESAI (100/100 PAKET TERKIRIM)!\n", batchNumber);
        Serial.println("   Pengiriman BERHENTI Otomatis untuk Pencatatan Data.");
        Serial.println("=================================================\n");
        updateLcdCollision();
      }
    }
  }
}
