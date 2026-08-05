/**
 * ============================================================
 * COLLISION TEST — NODE (dengan LCD 16x2)
 * ============================================================
 * Node mengirim data ke Gateway dengan interval yang bisa diatur.
 * 
 * WIRING:
 *   LoRa E32:
 *     VCC → 3.3V | GND → GND
 *     RX  → GPIO 17 (TX ESP32)
 *     TX  → GPIO 16 (RX ESP32)
 *     AUX → GPIO 18 | M0 → GPIO 19 | M1 → GPIO 23
 *
 *   LCD 16x2 I2C (PCF8574):
 *     VCC → 5V | GND → GND
 *     SDA → GPIO 21 | SCL → GPIO 22
 *
 * SETTING VIA SERIAL MONITOR:
 *   set 5   → set interval 5ms
 *   set 10  → set interval 10ms
 *   set 50  → set interval 50ms
 *   ida     → set sebagai Node A (default)
 *   idb     → set sebagai Node B
 *
 * ADDRESS:
 *   Node A:  0x0002
 *   Node B:  0x0003
 *   Gateway: 0x0001
 *   Channel: 0x17
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ===== PIN LoRa =====
#define AUX_PIN  18
#define M0_PIN   19  // pindah dari 21 (conflict I2C)
#define M1_PIN   23  // pindah dari 22 (conflict I2C)
#define RX_PIN   16
#define TX_PIN   17

// ===== PIN LCD I2C =====
#define LCD_SDA 21
#define LCD_SCL 22
#define LCD_ADDR 0x27  // atau 0x3F, cek dengan I2C scanner

// ===== LoRa Address =====
#define GW_ADDH    0x00
#define GW_ADDL    0x01
#define LORA_CHAN  0x17

HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Preferences prefs;

// ===== State =====
char nodeId = 'A';  // A atau B
uint8_t nodeAddL = 0x02;  // 0x02=Node A, 0x03=Node B
uint16_t seq = 0;
uint16_t intervalMs = 10;  // default 10ms
uint32_t totalSent = 0;
uint32_t totalFail = 0;
unsigned long lastSend = 0;
unsigned long lastLcdUpdate = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  // Load settings dari NVS
  prefs.begin("coltest", true);
  nodeId = prefs.getChar("node", 'A');
  intervalMs = prefs.getUInt("interval", 10);
  prefs.end();

  nodeAddL = (nodeId == 'A') ? 0x02 : 0x03;

  // Init LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("COLLISION TEST");
  lcd.setCursor(0, 1);
  lcd.printf("Node %c Starting", nodeId);

  // Init LoRa
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  delay(100);

  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code != 1) {
    Serial.println(F("[ERROR] LoRa E32 GAGAL!"));
    lcd.clear();
    lcd.print("LoRa GAGAL!");
    while (1) delay(1000);
  }

  Configuration cfg = *(Configuration*) c.data;
  cfg.ADDH = 0x00;
  cfg.ADDL = nodeAddL;
  cfg.CHAN = LORA_CHAN;
  cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  cfg.OPTION.fec               = FEC_1_ON;
  cfg.OPTION.transmissionPower = POWER_20;
  cfg.SPED.airDataRate         = AIR_DATA_RATE_010_24;
  cfg.SPED.uartBaudRate        = UART_BPS_9600;
  cfg.SPED.uartParity          = MODE_00_8N1;
  e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
  c.close();

  Serial.println(F("============================================"));
  Serial.println(F("  COLLISION TEST — NODE"));
  Serial.printf("  Node ID       : %c\n", nodeId);
  Serial.printf("  Address       : 0x00%02X\n", nodeAddL);
  Serial.printf("  Interval TX   : %u ms\n", intervalMs);
  Serial.println(F("============================================"));
  Serial.println(F("PERINTAH:"));
  Serial.println(F("  set 5   -> set interval 5ms"));
  Serial.println(F("  set 10  -> set interval 10ms"));
  Serial.println(F("  set 50  -> set interval 50ms"));
  Serial.println(F("  ida     -> set sebagai Node A"));
  Serial.println(F("  idb     -> set sebagai Node B"));
  Serial.println(F("============================================"));
  Serial.println(F("Seq,Sent,Fail,LossRate(%),TxDur(ms)"));

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("Node%c %ums", nodeId, intervalMs);
  lcd.setCursor(0, 1);
  lcd.print("Ready...");

  delay(2000);
  lastSend = millis();
  lastLcdUpdate = millis();
}

void loop() {
  // Cek perintah Serial
  if (Serial.available()) {
    handleCommand();
  }

  // Kirim paket sesuai interval
  if (millis() - lastSend >= intervalMs) {
    sendPacket();
    lastSend = millis();
  }

  // Update LCD setiap 500ms
  if (millis() - lastLcdUpdate > 500) {
    updateLCD();
    lastLcdUpdate = millis();
  }
}

void handleCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.startsWith("set ")) {
    int ms = cmd.substring(4).toInt();
    if (ms >= 5 && ms <= 50) {
      intervalMs = ms;
      prefs.begin("coltest", false);
      prefs.putUInt("interval", intervalMs);
      prefs.end();
      Serial.printf("[OK] Interval diubah ke %u ms\n", intervalMs);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.printf("Interval:%ums", intervalMs);
      lcd.setCursor(0, 1);
      lcd.print("Changed!");
      delay(1500);
    } else {
      Serial.println("[ERROR] Interval harus 5-50 ms");
    }
  }
  else if (cmd == "ida") {
    nodeId = 'A';
    nodeAddL = 0x02;
    saveAndRestart();
  }
  else if (cmd == "idb") {
    nodeId = 'B';
    nodeAddL = 0x03;
    saveAndRestart();
  }
  else {
    Serial.println("[?] Perintah tidak dikenal");
  }
}

void saveAndRestart() {
  prefs.begin("coltest", false);
  prefs.putChar("node", nodeId);
  prefs.end();
  Serial.printf("[OK] Node ID diubah ke %c, restarting...\n", nodeId);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("Set Node %c", nodeId);
  lcd.setCursor(0, 1);
  lcd.print("Restarting...");
  delay(2000);
  ESP.restart();
}

void sendPacket() {
  seq++;

  // Format: CTEST:<node_id>:<seq>:<interval_ms>
  String msg = "CTEST:" + String(nodeId) + ":" + String(seq) + ":" + String(intervalMs);

  unsigned long tStart = millis();
  ResponseStatus rs = e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CHAN, msg);
  unsigned long tEnd = millis();

  if (rs.code == 1) {
    totalSent++;
  } else {
    totalFail++;
  }

  uint32_t total = totalSent + totalFail;
  float lossRate = total > 0 ? (totalFail * 100.0 / total) : 0;

  Serial.printf("%u,%lu,%lu,%.1f,%lu\n",
    seq, (unsigned long)totalSent, (unsigned long)totalFail,
    lossRate, tEnd - tStart);
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("N%c %ums #%u", nodeId, intervalMs, seq);
  lcd.setCursor(0, 1);
  lcd.printf("TX:%lu F:%lu", (unsigned long)totalSent, (unsigned long)totalFail);
}
