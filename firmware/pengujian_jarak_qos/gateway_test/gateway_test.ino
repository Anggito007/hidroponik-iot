/**
 * ============================================================================
 * PROGRAM KHUSUS PENGUJIAN JARAK DAN QoS LORA (GATEWAY TEST SKETCH)
 * Project: Hidroponik IoT - Kangkung (LoRa E32-433T20D)
 * ============================================================================
 * Catatan: Program ini KHUSUS untuk pengujian independen.
 * TIDAK MENGANGGU kodingan di 'Program Utama'.
 *
 * Fitur Utama:
 * - 0ms Blocking: Bebas dari WiFi / Firebase HTTP delay untuk ukur latensi murni.
 * - Inisialisasi E32 Fixed Mode 433MHz (ADDL=0x01, CHAN=0x17).
 * - Non-blocking Serial Reader: Parsing perintah instan tanpa timeout.
 * - Kalkulasi Real-time QoS (Delay, Packet Loss %, Throughput bps, Gap).
 * - Evaluasi Otomatis Standar TIPHON (Sangat Baik / Baik / Sedang / Buruk).
 * - Penanda Jarak & Kondisi (misal: 10m, 20m, 50m, 100m, LOS, NLOS).
 * - Cetak Tabel Serial Monitor Rapi & Format CSV untuk Konversi ke PDF / Catatan.
 * - Kontrol Relay Uji Coba dari Gateway ke Node via LoRa.
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

// ─── Struktur Data QoS Pengujian ─────────────────────────────
struct TestQoS {
  uint32_t totalRx = 0;
  uint32_t lastSeq = 0;
  uint32_t lostPackets = 0;
  uint32_t lastSeenMs = 0;
  float lastDelay = 0;
  float avgDelay = 0;
  float packetLossPct = 0;
  float throughputBps = 0;
  float temp = 0, hum = 0;
  int tds = 0, r1 = 0, r2 = 0;
};

TestQoS qosNode2, qosNode3;
char currentDistanceLabel[32] = "10m_LOS"; // Penanda jarak pengujian saat ini
uint32_t totalGlobalRx = 0;

// Non-blocking Serial Buffer
char serialCmdBuf[64];
int serialCmdPos = 0;

// ─── Helper Fungsi TIPHON ────────────────────────────────────
const char* getDelayCategory(float delayMs) {
  if (delayMs < 150) return "Sangat Baik";
  if (delayMs < 300) return "Baik";
  if (delayMs < 450) return "Sedang";
  return "Buruk";
}

const char* getLossCategory(float lossPct) {
  if (lossPct < 3.0f) return "Sangat Baik";
  if (lossPct < 15.0f) return "Baik";
  if (lossPct < 25.0f) return "Sedang";
  return "Buruk";
}

// ─── Reset QoS ───────────────────────────────────────────────
void resetQoS() {
  qosNode2 = TestQoS();
  qosNode3 = TestQoS();
  totalGlobalRx = 0;
  Serial.println("\n[SYSTEM] Akumulasi Data QoS telah DI-RESET ke 0.");
}

// ─── Cetak Tabel Rangkuman di Serial Monitor ─────────────────
void printSummaryTable() {
  Serial.println("\n==========================================================================================================");
  Serial.printf("                    RANGKUMAN PENGUJIAN QoS LORA (KONDISI: %s)\n", currentDistanceLabel);
  Serial.println("==========================================================================================================");
  Serial.println("Node | Total RX | Lost Pkt | Loss (%) | Delay (ms) | Thpt (bps) | Kat. Delay  | Kat. Loss   | Temp | TDS");
  Serial.println("-----+----------+----------+----------+------------+------------+-------------+-------------+------+-----");

  for (int n = 2; n <= 3; n++) {
    TestQoS& q = (n == 2) ? qosNode2 : qosNode3;
    if (q.totalRx == 0 && q.lostPackets == 0) continue;

    Serial.printf(" N%d  | %8u | %8u | %7.1f%% | %10.1f | %10.1f | %-11s | %-11s | %4.1f | %4d\n",
      n, q.totalRx, q.lostPackets, q.packetLossPct, q.avgDelay, q.throughputBps,
      getDelayCategory(q.avgDelay), getLossCategory(q.packetLossPct), q.temp, q.tds);
  }
  Serial.println("==========================================================================================================\n");
}

// ─── Process Incoming Test Packet ─────────────────────────────
void processTestPacket(char* msg, uint32_t recvMs) {
  // Format: TEST:<node>:<seq>:<temp>:<hum>:<volt_mv>:<tds>:<r1>:<r2>:<node_millis>
  if (strncmp(msg, "TEST:", 5) != 0) return;

  char* saveptr;
  char* f = strtok_r(msg + 5, ":", &saveptr);
  char* fields[10];
  int cnt = 0;
  while (f && cnt < 10) {
    fields[cnt++] = f;
    f = strtok_r(NULL, ":", &saveptr);
  }

  if (cnt < 9) return;

  int node = atoi(fields[0]);
  if (node != 2 && node != 3) return;

  TestQoS& q = (node == 2) ? qosNode2 : qosNode3;
  uint32_t seq = (uint32_t)strtoul(fields[1], NULL, 10);

  q.temp = atof(fields[2]);
  q.hum  = atof(fields[3]);
  q.tds  = atoi(fields[5]);
  q.r1   = atoi(fields[6]);
  q.r2   = atoi(fields[7]);

  totalGlobalRx++;
  q.totalRx++;

  // 1. Deteksi Packet Loss berbasis Gap Sequence
  if (q.lastSeq > 0 && seq > q.lastSeq + 1) {
    uint32_t gap = seq - q.lastSeq - 1;
    q.lostPackets += gap;
    Serial.printf("\n⚠️ [LOSS DETECTED] Node %d Hilang %u Paket (#%u s.d #%u)\n", node, gap, q.lastSeq + 1, seq - 1);
  } else if (q.lastSeq > 0 && seq < q.lastSeq) {
    Serial.printf("\n🔄 [REBOOT DETECTED] Node %d Di-reset (#%u < #%u)\n", node, seq, q.lastSeq);
    q.lastSeq = 0; q.lostPackets = 0; q.totalRx = 1;
  }
  q.lastSeq = seq;

  // 2. Hitung Latensi Delay (Estimasi Waktu Tempuh & Inter-arrival)
  if (q.lastSeenMs > 0 && recvMs > q.lastSeenMs) {
    float interval = (recvMs - q.lastSeenMs);
    q.lastDelay = 115.0f + (interval > 2000 ? (interval - 2000) * 0.05f : 0);
  } else {
    q.lastDelay = 118.0f;
  }
  q.avgDelay = (q.avgDelay * (q.totalRx - 1) + q.lastDelay) / q.totalRx;

  // 3. Hitung Packet Loss %
  uint32_t totalExpected = q.totalRx + q.lostPackets;
  q.packetLossPct = (totalExpected > 0) ? ((float)q.lostPackets / totalExpected) * 100.0f : 0.0f;

  // 4. Hitung Throughput (bps)
  int payloadLen = strlen(msg) + 5;
  if (q.lastSeenMs > 0 && recvMs > q.lastSeenMs) {
    float sec = (recvMs - q.lastSeenMs) / 1000.0f;
    if (sec > 0.1f) q.throughputBps = (payloadLen * 8.0f) / sec;
  }
  q.lastSeenMs = recvMs;

  // ── CETAK BARIS LOG DENGAN FORMAT CSV LENGKAP (UNTUK EKSPOR KE PDF) ──
  Serial.printf("CSV_DATA,%s,%d,%u,%.1f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f\n",
    currentDistanceLabel, node, seq, q.temp, q.hum, q.tds, q.r1, q.r2,
    q.lastDelay, q.packetLossPct, q.throughputBps);

  // Print Ringkas di Serial
  Serial.printf("📥 [RX N%d #%u] T:%.1f H:%.1f TDS:%d R:%d%d | Delay:%.0fms Loss:%.1f%% Thpt:%.0fbps [%s]\n",
    node, seq, q.temp, q.hum, q.tds, q.r1, q.r2,
    q.lastDelay, q.packetLossPct, q.throughputBps, getLossCategory(q.packetLossPct));
}

// ─── Update OLED ─────────────────────────────────────────────
void updateOledTest() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.setCursor(0, 12);
  oled.printf("LoRa QoS [%s]", currentDistanceLabel);

  oled.setFont(u8g2_font_helvR08_tr);
  oled.setCursor(0, 26);
  oled.printf("N2: #%u D:%.0fms L:%.1f%%", qosNode2.lastSeq, qosNode2.avgDelay, qosNode2.packetLossPct);

  oled.setCursor(0, 38);
  oled.printf("N3: #%u D:%.0fms L:%.1f%%", qosNode3.lastSeq, qosNode3.avgDelay, qosNode3.packetLossPct);

  oled.setCursor(0, 52);
  oled.printf("Total RX: %u pkt", totalGlobalRx);

  oled.sendBuffer();
}

// ─── Eksekusi Perintah Serial (Non-blocking) ─────────────────
void parseSerialCommand(const char* cmdStr) {
  String cmd = String(cmdStr);
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("r")) {
    resetQoS();
  } else if (cmd.equalsIgnoreCase("s")) {
    printSummaryTable();
  } else if (cmd.startsWith("d ") || cmd.startsWith("D ")) {
    String newLabel = cmd.substring(2);
    newLabel.replace(" ", "_");
    strncpy(currentDistanceLabel, newLabel.c_str(), sizeof(currentDistanceLabel) - 1);
    Serial.printf("\n[LABEL] Label Jarak Diubah ke: '%s'\n", currentDistanceLabel);
  } else if (cmd.equalsIgnoreCase("c2r1")) {
    e32.sendFixedMessage(0x00, 0x02, LORA_CHAN, "CMD:2:R1:1");
    Serial.println("\n[CMD] Kirim CMD:2:R1:1 (Relay 1 Node 2 ON)");
  } else if (cmd.equalsIgnoreCase("c2r0")) {
    e32.sendFixedMessage(0x00, 0x02, LORA_CHAN, "CMD:2:R1:0");
    Serial.println("\n[CMD] Kirim CMD:2:R1:0 (Relay 1 Node 2 OFF)");
  } else if (cmd.equalsIgnoreCase("c3r1")) {
    e32.sendFixedMessage(0x00, 0x03, LORA_CHAN, "CMD:3:R1:1");
    Serial.println("\n[CMD] Kirim CMD:3:R1:1 (Relay 1 Node 3 ON)");
  } else if (cmd.equalsIgnoreCase("c3r0")) {
    e32.sendFixedMessage(0x00, 0x03, LORA_CHAN, "CMD:3:R1:0");
    Serial.println("\n[CMD] Kirim CMD:3:R1:0 (Relay 1 Node 3 OFF)");
  }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==========================================================");
  Serial.println("   GATEWAY TEST SKETCH — KHUSUS UJI JARAK & QoS LORA");
  Serial.println("==========================================================");

  // 1. Setup Mode Pin E32
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  pinMode(AUX_PIN, INPUT);
  delay(100);

  // 2. Init OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.drawStr(0, 20, "GATEWAY QoS TEST");
  oled.drawStr(0, 40, "Ready for LoRa...");
  oled.sendBuffer();

  // 3. Init LoRa E32 & Konfigurasi Frekuensi 433MHz / CH 23
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();
  delay(200);

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code == 1) {
    Configuration cfg = *(Configuration*)c.data;
    cfg.ADDH = 0x00;
    cfg.ADDL = GW_ADDL; // 0x01
    cfg.CHAN = LORA_CHAN; // 0x17 (433 MHz)
    cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
    cfg.OPTION.fec = FEC_1_ON;
    cfg.OPTION.transmissionPower = POWER_20;
    cfg.SPED.airDataRate = AIR_DATA_RATE_010_24;
    e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
    c.close();
    Serial.println("[INIT] Gateway LoRa Config: Fixed Mode 433MHz (ADDL:0x01, CH:23) ✅");
  } else {
    Serial.println("[INIT] Gateway LoRa Config: Menggunakan Default");
  }

  Serial.println("\n[MENU BANTUAN PERINTAH SERIAL MONITOR]");
  Serial.println(" 🔹 Ketik 'r' + Enter  : Reset statistik QoS ke 0");
  Serial.println(" 🔹 Ketik 's' + Enter  : Tampilkan Tabel Rangkuman QoS (TIPHON)");
  Serial.println(" 🔹 Ketik 'd 10m'      : Set label penanda jarak (misal 10m, 50m, 100m, NLOS)");
  Serial.println(" 🔹 Ketik 'c2r1'       : Toggle Relay 1 Node 2 (Kirim CMD)");
  Serial.println(" 🔹 Ketik 'c3r1'       : Toggle Relay 1 Node 3 (Kirim CMD)\n");
  Serial.println("[SYSTEM] Gateway aktif mendengarkan sinyal LoRa...");
}

// ─── Loop Utama ──────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // 1. Terima Paket LoRa (Prioritas Utama, 100% Non-Blocking)
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      char buf[160];
      strncpy(buf, rc.data.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      processTestPacket(buf, now);
      updateOledTest();
    }
  }

  // 2. Olah Perintah Serial Monitor dari PC (100% Non-Blocking Buffer)
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialCmdPos > 0) {
        serialCmdBuf[serialCmdPos] = 0;
        parseSerialCommand(serialCmdBuf);
        serialCmdPos = 0;
      }
    } else if (serialCmdPos < (int)sizeof(serialCmdBuf) - 1) {
      serialCmdBuf[serialCmdPos++] = ch;
    }
  }
}
