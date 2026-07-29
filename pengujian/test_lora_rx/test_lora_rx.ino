/**
 * ============================================================
 * TEST LoRa E32 — RECEIVER (GATEWAY)
 * ============================================================
 * Menerima paket dari Node Transmitter untuk pengujian:
 *   - Jumlah paket diterima
 *   - Packet loss
 *   - Delay end-to-end
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
 *   Address Gateway : 0x0001
 *   Channel         : 0x17 (433 MHz)
 *
 * OUTPUT (Serial Monitor 115200 baud):
 *   No,Seq_Rx,Total_Rx,Total_Lost,LossRate(%),Delay_Node(ms),Status
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>

#define AUX_PIN  18
#define M0_PIN   21
#define M1_PIN   22
#define RX_PIN   16
#define TX_PIN   17

#define GW_ADDH    0x00
#define GW_ADDL    0x01
#define LORA_CHAN  0x17

HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);

uint32_t totalRx = 0;
uint16_t lastSeq = 0;
uint32_t totalLost = 0;
bool firstPacket = true;
unsigned long lastRxTime = 0;

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

  Serial.println(F("============================================"));
  Serial.println(F("  TEST LoRa E32 — RECEIVER (GATEWAY)"));
  Serial.printf("  Gateway Address : 0x%02X%02X\n", GW_ADDH, GW_ADDL);
  Serial.printf("  Channel         : 0x%02X\n", LORA_CHAN);
  Serial.println(F("  Menunggu paket dari Node..."));
  Serial.println(F("============================================"));
  Serial.println(F("No,Seq_Rx,Total_Rx,Total_Lost,LossRate(%),Delay_Antar_Paket(ms),Pesan"));
}

void loop() {
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      String msg = rc.data;
      msg.trim();

      // Parse: TEST:<seq>:<timestamp_node>
      if (msg.startsWith("TEST:")) {
        int c1 = msg.indexOf(':');
        int c2 = msg.indexOf(':', c1+1);

        if (c1 > 0 && c2 > 0) {
          uint16_t seq = msg.substring(c1+1, c2).toInt();

          totalRx++;
          unsigned long now = millis();
          unsigned long delayBetween = firstPacket ? 0 : (now - lastRxTime);

          // Hitung paket hilang berdasarkan gap di seq
          if (!firstPacket && seq > lastSeq + 1) {
            uint32_t lost = seq - lastSeq - 1;
            totalLost += lost;
          }

          lastSeq = seq;
          lastRxTime = now;
          firstPacket = false;

          uint32_t totalExpected = totalRx + totalLost;
          float lossRate = totalExpected > 0 ? (totalLost * 100.0 / totalExpected) : 0;

          Serial.printf("%lu,%u,%lu,%lu,%.1f,%lu,%s\n",
            (unsigned long)totalRx, seq,
            (unsigned long)totalRx, (unsigned long)totalLost,
            lossRate, delayBetween, msg.c_str());
        }
      } else {
        Serial.printf("[?] Pesan asing: %s\n", msg.c_str());
      }
    }
  }
}
