/**
 * ============================================================
 * COLLISION TEST — GATEWAY (dengan OLED)
 * ============================================================
 * Gateway menerima data dari 2 Node untuk analisis collision.
 * 
 * WIRING:
 *   LoRa E32:
 *     VCC → 3.3V | GND → GND
 *     RX  → GPIO 17 (TX ESP32)
 *     TX  → GPIO 16 (RX ESP32)
 *     AUX → GPIO 18 | M0 → GPIO 21 | M1 → GPIO 22
 *
 *   OLED 128x64 I2C (SSD1306):
 *     VCC → 3.3V | GND → GND
 *     SDA → GPIO 21 | SCL → GPIO 22
 *
 * ADDRESS:
 *   Gateway: 0x0001
 *   Node A:  0x0002
 *   Node B:  0x0003
 *   Channel: 0x17
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== PIN LoRa =====
#define AUX_PIN  18
#define M0_PIN   19  // pindah dari 21 (conflict I2C)
#define M1_PIN   23  // pindah dari 22 (conflict I2C)
#define RX_PIN   16
#define TX_PIN   17

// ===== PIN OLED I2C =====
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_RST -1

// ===== LoRa Address =====
#define GW_ADDH    0x00
#define GW_ADDL    0x01
#define LORA_CHAN  0x17

HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RST);

// ===== State =====
struct NodeStat {
  uint32_t totalRx;
  uint32_t totalLost;
  uint16_t lastSeq;
  unsigned long lastRxTime;
  bool firstPacket;
  uint16_t intervalMs;
} nodeA, nodeB;

uint32_t collisionCount = 0;
uint32_t totalPackets = 0;
unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  // Init OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERROR] OLED gagal!"));
    while (1) delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("COLLISION TEST"));
  display.println(F("Gateway Starting..."));
  display.display();

  // Init LoRa
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  delay(100);

  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code != 1) {
    Serial.println(F("[ERROR] LoRa E32 GAGAL!"));
    display.println(F("LoRa GAGAL!"));
    display.display();
    while (1) delay(1000);
  }

  Configuration cfg = *(Configuration*) c.data;
  cfg.ADDH = GW_ADDH;
  cfg.ADDL = GW_ADDL;
  cfg.CHAN = LORA_CHAN;
  cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  cfg.OPTION.fec               = FEC_1_ON;
  cfg.OPTION.transmissionPower = POWER_20;
  cfg.SPED.airDataRate         = AIR_DATA_RATE_010_24;
  cfg.SPED.uartBaudRate        = UART_BPS_9600;
  cfg.SPED.uartParity          = MODE_00_8N1;
  e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
  c.close();

  // Init state
  nodeA.firstPacket = true;
  nodeB.firstPacket = true;

  Serial.println(F("============================================"));
  Serial.println(F("  COLLISION TEST — GATEWAY"));
  Serial.println(F("  Menunggu data dari Node A & B..."));
  Serial.println(F("============================================"));
  Serial.println(F("Time,Node,Seq,Interval(ms),RxA,LostA,RxB,LostB,Collision,Total"));

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("GATEWAY READY"));
  display.println(F("Waiting nodes..."));
  display.display();

  lastUpdate = millis();
}

void loop() {
  // Terima paket LoRa
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      parsePacket(rc.data);
    }
  }

  // Update OLED setiap 500ms
  if (millis() - lastUpdate > 500) {
    updateDisplay();
    lastUpdate = millis();
  }
}

void parsePacket(String msg) {
  msg.trim();
  // Format: CTEST:<node_id>:<seq>:<interval_ms>
  if (!msg.startsWith("CTEST:")) return;

  int c1 = msg.indexOf(':', 6);
  int c2 = msg.indexOf(':', c1+1);
  if (c1 < 0 || c2 < 0) return;

  char nodeId = msg.charAt(6);
  uint16_t seq = msg.substring(c1+1, c2).toInt();
  uint16_t interval = msg.substring(c2+1).toInt();
  unsigned long now = millis();

  NodeStat *node = (nodeId == 'A') ? &nodeA : (nodeId == 'B') ? &nodeB : nullptr;
  if (!node) return;

  node->totalRx++;
  totalPackets++;

  // Hitung packet loss berdasarkan gap sequence
  if (!node->firstPacket && seq > node->lastSeq + 1) {
    uint32_t lost = seq - node->lastSeq - 1;
    node->totalLost += lost;
  }

  // Deteksi collision: kedua node RX dalam waktu <10ms
  if (nodeA.lastRxTime > 0 && nodeB.lastRxTime > 0) {
    unsigned long gap = (now > nodeA.lastRxTime && now > nodeB.lastRxTime)
      ? abs((long)(nodeA.lastRxTime - nodeB.lastRxTime))
      : 0;
    if (gap < 10) {
      collisionCount++;
    }
  }

  node->lastSeq = seq;
  node->lastRxTime = now;
  node->intervalMs = interval;
  node->firstPacket = false;

  // Log CSV
  Serial.printf("%lu,%c,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu\n",
    now, nodeId, seq, interval,
    (unsigned long)nodeA.totalRx, (unsigned long)nodeA.totalLost,
    (unsigned long)nodeB.totalRx, (unsigned long)nodeB.totalLost,
    (unsigned long)collisionCount, (unsigned long)totalPackets);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  // Header
  display.println(F("COLLISION TEST GW"));
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  // Node A
  display.setCursor(0, 12);
  display.printf("A:%lums Rx:%lu L:%lu\n",
    (unsigned long)nodeA.intervalMs,
    (unsigned long)nodeA.totalRx,
    (unsigned long)nodeA.totalLost);

  // Node B
  display.printf("B:%lums Rx:%lu L:%lu\n",
    (unsigned long)nodeB.intervalMs,
    (unsigned long)nodeB.totalRx,
    (unsigned long)nodeB.totalLost);

  display.drawLine(0, 37, 128, 37, SSD1306_WHITE);

  // Collision
  display.setCursor(0, 40);
  display.printf("Collision: %lu\n", (unsigned long)collisionCount);
  display.printf("Total Pkt: %lu\n", (unsigned long)totalPackets);

  // Collision rate
  float colRate = totalPackets > 0 ? (collisionCount * 100.0 / totalPackets) : 0;
  display.printf("Col Rate: %.1f%%", colRate);

  display.display();
}
