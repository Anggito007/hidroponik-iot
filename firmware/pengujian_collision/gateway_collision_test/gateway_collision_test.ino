/**
 * ============================================================================
 * PROGRAM KHUSUS PENGUJIAN COLLISION (TABRAKAN SINYAL LORA) — GATEWAY SKETCH
 * Project: Hidroponik IoT - Kangkung (LoRa E32-433T20D)
 * ============================================================================
 * Catatan: Program ini KHUSUS untuk Pengujian Tabrakan Sinyal (Collision).
 * 100% TERISOLASI di folder baru, TIDAK MENGANGGU 'Program Utama'.
 *
 * Fitur Utama Pengujian Sesuai Skenario:
 * - Master Broadcast Trigger dengan Variasi Lead Node:
   - trig 10  ➔ Node 2 Mulai Duluan (Node 3 Offset +10ms)
   - trig3 10 ➔ Node 3 Mulai Duluan (Node 2 Offset +10ms)
 * - Menghitung Hasil Penerimaan Paket per Batch (Target 100 Paket per Node).
 * - Menghitung Persentase Tabrakan Sinyal (Collision Loss Rate %).
 * ============================================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <U8g2lib.h>
#include <Wire.h>

// ─── Pin Definitions (ESP32 Gateway) ────────────────────────
#define AUX_PIN   18
#define M0_PIN    21
#define M1_PIN    22
#define RX_PIN    16
#define TX_PIN    17
#define OLED_SDA   4
#define OLED_SCL   5

#define GW_ADDL   0x01
#define LORA_CHAN 0x17

// ─── Objek Perangkat ─────────────────────────────────────────
HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ─── Struktur Data Batch Collision ───────────────────────────
struct CollisionBatch {
  uint16_t batchId = 1;
  int leadNode = 2;
  int offsetMs = 0;
  uint16_t rxNode2 = 0;
  uint16_t rxNode3 = 0;
  uint16_t lastSeqNode2 = 0;
  uint16_t lastSeqNode3 = 0;
};

CollisionBatch currentBatch;
char distanceConditionLabel[32] = "5m_LOS";
uint32_t totalCollisionRx = 0;

// Non-blocking Serial Buffer
char serialBuf[64];
int serialPos = 0;

// ─── Broadcast Master Sync Trigger ke Semua Node ──────────────
void triggerBatchSync(int leadNode, int offsetMs) {
  uint16_t nextId = (currentBatch.rxNode2 > 0 || currentBatch.rxNode3 > 0) ? currentBatch.batchId + 1 : currentBatch.batchId;
  currentBatch = CollisionBatch();
  currentBatch.batchId = nextId;
  currentBatch.leadNode = leadNode;
  currentBatch.offsetMs = offsetMs;

  char trigCmd[30];
  snprintf(trigCmd, sizeof(trigCmd), "TRIG:%d:%d", leadNode, offsetMs);

  // Broadcast (ADDH=0xFF, ADDL=0xFF) ke seluruh Node
  e32.sendBroadcastFixedMessage(LORA_CHAN, trigCmd);

  Serial.println("\n==========================================================================================================");
  Serial.printf("📡 [MASTER SYNC] MEMANCARKAN BATCH #%u (LEAD NODE: %d | OFFSET NODE PENDAMPING: +%d ms)\n",
    nextId, leadNode, offsetMs);
  Serial.println("   Kedua Node (Node 2 & Node 3) Diperintahkan Mengirim 100 Paket Data...");
  Serial.println("==========================================================================================================\n");
}

// ─── Cetak Tabel Rangkuman Hasil Pengujian Batch ─────────────
void printCollisionSummary() {
  uint16_t totalExpected = 200; // 100 paket N2 + 100 paket N3
  uint16_t totalReceived = currentBatch.rxNode2 + currentBatch.rxNode3;
  uint16_t totalLost = (100 - currentBatch.rxNode2) + (100 - currentBatch.rxNode3);
  float collisionLossPct = ((float)totalLost / totalExpected) * 100.0f;

  Serial.println("\n==========================================================================================================");
  Serial.printf("       TABEL HASIL PENGUJIAN COLLISION — BATCH #%u (LEAD NODE: %d | OFFSET PENDAMPING: +%d ms)\n",
    currentBatch.batchId, currentBatch.leadNode, currentBatch.offsetMs);
  Serial.printf("       Kondisi: %s | Target: 100 Paket / Node (Total 200 Paket)\n", distanceConditionLabel);
  Serial.println("==========================================================================================================");
  Serial.println(" Perangkat  | Peran Node  | Target Paket | Paket Diterima | Paket Hilang | Loss Rate (%) | Status Receiver");
  Serial.println("------------+-------------+--------------+----------------+--------------+---------------+----------------");
  Serial.printf(" Node 2     | %-11s |   100 Paket  |   %3u Paket    |  %3u Paket   |    %5.1f%%    | %s\n",
    (currentBatch.leadNode == 2 ? "LEAD (0ms)" : "FOLLOWER"), currentBatch.rxNode2, (100 - currentBatch.rxNode2),
    (100.0f - currentBatch.rxNode2), currentBatch.rxNode2 >= 95 ? "Lolos Sempurna ✅" : "Terjadi Tabrakan ⚠️");
  Serial.printf(" Node 3     | %-11s |   100 Paket  |   %3u Paket    |  %3u Paket   |    %5.1f%%    | %s\n",
    (currentBatch.leadNode == 3 ? "LEAD (0ms)" : "FOLLOWER"), currentBatch.rxNode3, (100 - currentBatch.rxNode3),
    (100.0f - currentBatch.rxNode3), currentBatch.rxNode3 >= 95 ? "Lolos Sempurna ✅" : "Terjadi Tabrakan ⚠️");
  Serial.println("------------+-------------+--------------+----------------+--------------+---------------+----------------");
  Serial.printf(" COMBINED   | BOTH NODES  |   200 Paket  |   %3u Paket    |  %3u Paket   |    %5.1f%%    | KESELURUHAN BATCH\n",
    totalReceived, totalLost, collisionLossPct);
  Serial.println("==========================================================================================================\n");
}

// ─── Process Incoming Collision Packet ───────────────────────
void processCollisionPacket(char* msg, uint32_t nowMs) {
  // Format: COL:<node>:<batch_seq>:<offset_ms>:<temp>:<hum>:<tds>:<millis>
  if (strncmp(msg, "COL:", 4) != 0) return;

  char* saveptr;
  char* f = strtok_r(msg + 4, ":", &saveptr);
  char* fields[8];
  int cnt = 0;
  while (f && cnt < 8) {
    fields[cnt++] = f;
    f = strtok_r(NULL, ":", &saveptr);
  }

  if (cnt < 7) return;

  int node = atoi(fields[0]);
  uint16_t bSeq = (uint16_t)strtoul(fields[1], NULL, 10);
  int offset = atoi(fields[2]);

  // Update offset follower node
  if (node != currentBatch.leadNode && offset > 0) {
    currentBatch.offsetMs = offset;
  }
  totalCollisionRx++;

  if (node == 2) {
    currentBatch.rxNode2++;
    currentBatch.lastSeqNode2 = bSeq;
  } else if (node == 3) {
    currentBatch.rxNode3++;
    currentBatch.lastSeqNode3 = bSeq;
  }

  // Baris Log CSV
  Serial.printf("CSV_COLLISION,%s,%u,%d,%d,%d,%u,%u,%u\n",
    distanceConditionLabel, currentBatch.batchId, currentBatch.leadNode, currentBatch.offsetMs, node, bSeq,
    currentBatch.rxNode2, currentBatch.rxNode3);

  // Print Log ke Serial
  Serial.printf("📥 [COL-RX N%d Pkt %3d/100] (Lead:N%d | Off +%dms) | Diterima -> N2:%u/100 N3:%u/100\n",
    node, bSeq, currentBatch.leadNode, currentBatch.offsetMs, currentBatch.rxNode2, currentBatch.rxNode3);

  // Jika paket ke-100 dari kedua Node sudah selesai, tampilkan rangkuman otomatis
  if (bSeq == 100 && (currentBatch.rxNode2 >= 100 || currentBatch.rxNode3 >= 100)) {
    printCollisionSummary();
  }
}

// ─── Update OLED Display ─────────────────────────────────────
void updateOledCollision() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.setCursor(0, 12);
  oled.printf("Collision [%s]", distanceConditionLabel);

  oled.setFont(u8g2_font_helvR08_tr);
  oled.setCursor(0, 28);
  oled.printf("Batch #%u (Lead:N%d Off:+%dms)", currentBatch.batchId, currentBatch.leadNode, currentBatch.offsetMs);

  oled.setCursor(0, 42);
  oled.printf("N2: %u/100 | N3: %u/100", currentBatch.rxNode2, currentBatch.rxNode3);

  oled.setCursor(0, 56);
  uint16_t totalLoss = (100 - currentBatch.rxNode2) + (100 - currentBatch.rxNode3);
  oled.printf("Total Lost: %u pkt", totalLoss);

  oled.sendBuffer();
}

// ─── Parse Serial Command ────────────────────────────────────
void parseCommand(const char* cmdStr) {
  String cmd = String(cmdStr);
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("trig3 ") || cmd.startsWith("TRIG3 ")) {
    int val = cmd.substring(6).toInt();
    triggerBatchSync(3, val);  // Node 3 Lead (0ms), Node 2 Follower (+val ms)
  } else if (cmd.startsWith("trig ") || cmd.startsWith("TRIG ")) {
    int val = cmd.substring(5).toInt();
    triggerBatchSync(2, val);  // Node 2 Lead (0ms), Node 3 Follower (+val ms)
  } else if (cmd.equalsIgnoreCase("s") || cmd.equalsIgnoreCase("summary")) {
    printCollisionSummary();
  } else if (cmd.equalsIgnoreCase("r") || cmd.equalsIgnoreCase("reset")) {
    currentBatch = CollisionBatch();
    Serial.println("\n[SYSTEM] Akumulasi Batch Di-reset.");
  } else if (cmd.startsWith("d ") || cmd.startsWith("D ")) {
    String label = cmd.substring(2);
    label.replace(" ", "_");
    strncpy(distanceConditionLabel, label.c_str(), sizeof(distanceConditionLabel) - 1);
    Serial.printf("\n[LABEL] Label Kondisi Diubah ke: '%s'\n", distanceConditionLabel);
  }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==========================================================");
  Serial.println("   GATEWAY COLLISION TEST SKETCH — VARIASI LEAD NODE");
  Serial.println("==========================================================");

  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  pinMode(AUX_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.drawStr(0, 20, "COLLISION TEST");
  oled.drawStr(0, 40, "Ready for LoRa...");
  oled.sendBuffer();

  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();
  delay(200);

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code == 1) {
    Configuration cfg = *(Configuration*)c.data;
    cfg.ADDH = 0x00; cfg.ADDL = GW_ADDL; cfg.CHAN = LORA_CHAN;
    cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
    cfg.OPTION.fec = FEC_1_ON;
    cfg.OPTION.transmissionPower = POWER_20;
    cfg.SPED.airDataRate = AIR_DATA_RATE_010_24;
    e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
    c.close();
    Serial.println("[INIT] Gateway LoRa Configured: Fixed 433MHz (ADDL:0x01, CH:23) ✅");
  }

  Serial.println("\n[MENU KONTROL PEMICU SIMULTAN & VARIASI LEAD NODE]");
  Serial.println(" 🔹 Ketik 'trig 10'  ➔ Node 2 Mulai Duluan (Node 3 Offset +10 ms)");
  Serial.println(" 🔹 Ketik 'trig3 10' ➔ Node 3 Mulai Duluan (Node 2 Offset +10 ms)");
  Serial.println(" 🔹 Ketik 's'        ➔ Tampilkan Tabel Rangkuman Hasil Batch");
  Serial.println(" 🔹 Ketik 'r'        ➔ Reset Batch ke 0\n");
}

// ─── Loop Utama ──────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // 1. Terima Paket LoRa Collision
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      char buf[160];
      strncpy(buf, rc.data.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      processCollisionPacket(buf, now);
      updateOledCollision();
    }
  }

  // 2. Olah Perintah Serial Monitor (Non-Blocking)
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialPos > 0) {
        serialBuf[serialPos] = 0;
        parseCommand(serialBuf);
        serialPos = 0;
      }
    } else if (serialPos < (int)sizeof(serialBuf) - 1) {
      serialBuf[serialPos++] = ch;
    }
  }
}
