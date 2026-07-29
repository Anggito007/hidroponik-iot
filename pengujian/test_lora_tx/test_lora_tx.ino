/**
 * ============================================================
 * TEST LoRa E32 — TRANSMITTER (NODE)
 * ============================================================
 * Mengirim paket data berurutan ke Gateway untuk pengujian:
 *   - Jarak komunikasi
 *   - Packet loss
 *   - Delay transmisi
 *
 * WIRING:
 *   LoRa VCC → 3.3V (WAJIB, jangan 5V!)
 *   LoRa GND → GND
 *   LoRa RX  → GPIO 17 (TX ESP32)
 *   LoRa TX  → GPIO 16 (RX ESP32)
 *   LoRa AUX → GPIO 18
 *   LoRa M0  → GPIO 21
 *   LoRa M1  → GPIO 22
 *
 * KONFIGURASI:
 *   Address Node    : 0x0002
 *   Address Gateway : 0x0001
 *   Channel         : 0x17 (433 MHz)
 *
 * OUTPUT (Serial Monitor 115200 baud):
 *   No,Seq,Durasi_TX(ms),Status
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>

#define AUX_PIN  18
#define M0_PIN   21
#define M1_PIN   22
#define RX_PIN   16
#define TX_PIN   17

#define NODE_ADDH    0x00
#define NODE_ADDL    0x02
#define GW_ADDH      0x00
#define GW_ADDL      0x01
#define LORA_CHAN    0x17

#define SEND_INTERVAL 2000  // ms — ganti untuk uji interval

HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);

uint16_t seq = 0;
uint32_t totalSent = 0;
uint32_t totalFail = 0;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  delay(100);

  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code != 1) {
    Serial.println(F("[ERROR] LoRa E32 GAGAL! Cek wiring."));
    while (1) delay(1000);
  }

  Configuration cfg = *(Configuration*) c.data;
  cfg.ADDH = NODE_ADDH;
  cfg.ADDL = NODE_ADDL;
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
  Serial.println(F("  TEST LoRa E32 — TRANSMITTER (NODE)"));
  Serial.printf("  Node Address    : 0x%02X%02X\n", NODE_ADDH, NODE_ADDL);
  Serial.printf("  Gateway Address : 0x%02X%02X\n", GW_ADDH, GW_ADDL);
  Serial.printf("  Channel         : 0x%02X\n", LORA_CHAN);
  Serial.printf("  Send Interval   : %d ms\n", SEND_INTERVAL);
  Serial.println(F("============================================"));
  Serial.println(F("No,Seq,Durasi_TX(ms),Sent,Fail,LossRate(%),Status"));
}

void loop() {
  if (millis() - lastSend < SEND_INTERVAL) return;
  lastSend = millis();

  seq++;

  // Format pesan: TEST:<seq>:<timestamp_ms>
  String msg = "TEST:" + String(seq) + ":" + String(millis());

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

  Serial.printf("%lu,%u,%lu,%lu,%lu,%.1f,%s\n",
    (unsigned long)total, seq, tEnd - tStart,
    (unsigned long)totalSent, (unsigned long)totalFail,
    lossRate, rs.code == 1 ? "OK" : "GAGAL");
}
