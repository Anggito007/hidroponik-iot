/**
 * HydroIoT NODE 2 — Hydroponic A
 * TX setiap 7 detik ke Gateway via LoRa E32
 *
 * FIX v3 — high-PPM LoRa TX:NG fix:
 *  Root cause: library LoRa_E32 sendFixedMessage() gagal (return
 *  code != 1) saat probe TDS tercelup di larutan high-PPM.
 *  Solusi: BYPASS library untuk pengiriman. Library hanya dipakai
 *  untuk konfigurasi awal E32 (address, channel, power).
 *  Semua TX runtime menggunakan direct UART write.
 *
 * FIX v4 — Relay control audit & fix:
 *  ROOT CAUSE: Relay tidak pernah bisa aktif karena:
 *  BUG #1: Relay state di-hardcode "0,0,0" di message LoRa (line 236)
 *          → gateway dan dashboard selalu melihat relay OFF.
 *  BUG #2: Tidak ada relay driver. relay pins di-set HIGH di setup()
 *          tapi TIDAK ADA fungsi apapun yang pernah menulis LOW.
 *          Tidak ada processLoRaRx(), tidak ada relay command handler.
 *  BUG #3: OTA handler ada tapi TIDAK dipanggil.
 *
 *  Fix: Tambahkan relay driver lengkap (init, on, off, toggle),
 *  self-test saat boot, dan LoRa command receiver untuk menerima
 *  CMD relay dari gateway.
 */
#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <DHT.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_system.h>
#include <driver/rtc_io.h>
#include "../common/tds_calibration.h"

// ─── Pin Definitions ────────────────────────────────────────
#define AUX_PIN   18
#define M0_PIN    21
#define M1_PIN    22
#define RX_PIN    16
#define TX_PIN    17
#define DHT_PIN    4
#define TDS_PIN   39
#define R1_PIN    25
#define R2_PIN    26
#define LCD_SDA   13
#define LCD_SCL   14
#define LCD_ADDR  0x27

// ─── LoRa Config ────────────────────────────────────────────
#define NODE_ADDL  0x02
#define GW_ADDL    0x01
#define LORA_CHAN  0x17
#define SEND_MS    5000

// ─── DHT22 Kalibrasi ────────────────────────────────────────
// Offset suhu hasil kalibrasi terhadap termometer referensi
// Referensi: 26.2°C | DHT22: 23.0°C | Offset: +3.2°C
#define DHT_TEMP_OFFSET  3.2f

// ─── Objects ────────────────────────────────────────────────
HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
DHT dht(DHT_PIN, DHT22);
Preferences prefs;
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
TDSCal tds;

// ─── State ──────────────────────────────────────────────────
uint16_t seq = 0;
unsigned long lastSend = 0;
bool lastOk = false;
unsigned long lcdLast = 0;

// Pipeline: sampled AFTER TX, sent in NEXT cycle
float curTemp = -99, curHum = -99;
float curVolt = 0, curPpm = 0;
float txTemp = -99, txHum = -99;
float txVolt = 0, txPpm = 0;
int txTds = 0;

// TX statistics for LCD
uint32_t txTotal = 0, txFail = 0;

// Autonomous Control (Edge Computing)
uint8_t modeOperasi = 0;  // 0 = AUTO, 1 = MANUAL
#define TEMP_KIPAS_ON   30.0    // Suhu ON kipas (°C)
#define TEMP_KIPAS_OFF  27.0    // Suhu OFF kipas (°C)
#define TDS_POMPA_ON    700     // PPM batas bawah ON pompa
#define DOSING_MS       30000   // Durasi pompa ON (30 detik)
#define DOSING_COOLDOWN 600000  // Cooldown 10 menit (ms)
unsigned long dosingStart = 0;    // Waktu mulai dosing
unsigned long cooldownStart = 0;  // Waktu mulai cooldown
bool dosingActive = false;        // Pompa sedang menyala
bool dosingCooldown = false;      // Sedang dalam periode cooldown

// ════════════════════════════════════════════════════════════
//  RELAY DRIVER — Active LOW (LOW = ON, HIGH = OFF)
// ════════════════════════════════════════════════════════════

#define RELAY_COUNT 2
const uint8_t RELAY_PINS[RELAY_COUNT] = {R1_PIN, R2_PIN};
bool relayState[RELAY_COUNT] = {false, false};  // false = OFF

/**
 * Inisialisasi semua relay pin. Harus dipanggil di setup().
 * Urutan: digitalWrite(HIGH) dulu, LALU pinMode(OUTPUT).
 * Ini mencegah glitch LOW sesaat saat boot yang menyalakan relay.
 */
void relayInit() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    digitalWrite(RELAY_PINS[i], HIGH);  // Set HIGH SEBELUM output mode
    pinMode(RELAY_PINS[i], OUTPUT);     // Baru jadikan output
    relayState[i] = false;
  }
  Serial.println("[RELAY] Init complete — all relays OFF");
}

/**
 * Nyalakan relay tertentu (1-indexed: relay 1, 2, 3)
 * Active LOW: menulis LOW ke pin.
 */
void relayOn(uint8_t relay) {
  if (relay < 1 || relay > RELAY_COUNT) return;
  uint8_t idx = relay - 1;
  digitalWrite(RELAY_PINS[idx], LOW);   // Active LOW = ON
  relayState[idx] = true;
  Serial.printf("[RELAY] Relay %d ON  (GPIO%d = LOW)\n", relay, RELAY_PINS[idx]);
}

/**
 * Matikan relay tertentu (1-indexed)
 * Active LOW: menulis HIGH ke pin.
 */
void relayOff(uint8_t relay) {
  if (relay < 1 || relay > RELAY_COUNT) return;
  uint8_t idx = relay - 1;
  digitalWrite(RELAY_PINS[idx], HIGH);  // Active LOW = OFF
  relayState[idx] = false;
  Serial.printf("[RELAY] Relay %d OFF (GPIO%d = HIGH)\n", relay, RELAY_PINS[idx]);
}

/**
 * Toggle relay tertentu (1-indexed)
 */
void relayToggle(uint8_t relay) {
  if (relay < 1 || relay > RELAY_COUNT) return;
  uint8_t idx = relay - 1;
  if (relayState[idx]) relayOff(relay);
  else relayOn(relay);
}

/**
 * Set relay berdasarkan nomor dan state boolean.
 * Dipakai oleh command handler.
 */
void relaySet(uint8_t relay, bool on) {
  if (on) relayOn(relay);
  else relayOff(relay);
}

/**
 * Update relay state dari tiga boolean (untuk update massal).
 * Hanya mengubah relay yang state-nya berbeda.
 */
void relayUpdate(bool r1, bool r2) {
  bool desired[2] = {r1, r2};
  for (int i = 0; i < RELAY_COUNT; i++) {
    if (desired[i] != relayState[i]) {
      relaySet(i + 1, desired[i]);
    }
  }
}

/**
 * Print status semua relay ke Serial.
 */
void relayPrintStatus() {
  Serial.printf("[RELAY] Status: R1=%s R2=%s\n",
    relayState[0] ? "ON" : "OFF",
    relayState[1] ? "ON" : "OFF");
}

/**
 * Self-test: nyalakan setiap relay 500ms lalu matikan.
 * Panggil sekali di akhir setup() untuk verifikasi hardware.
 */
void relaySelfTest() {
  Serial.println("[RELAY] ═══ Self-test started ═══");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Relay SelfTest");

  for (int r = 1; r <= RELAY_COUNT; r++) {
    lcd.setCursor(0, 1);
    char buf[17];
    snprintf(buf, sizeof(buf), "Relay %d: ON", r);
    lcd.print(buf);

    relayOn(r);
    delay(500);
    relayOff(r);
    delay(200);   // short pause between relays
  }

  relayPrintStatus();
  Serial.println("[RELAY] ═══ Self-test completed ═══");
  lcd.setCursor(0, 1); lcd.print("Test OK         ");
  delay(500);
}

// ════════════════════════════════════════════════════════════
//  SENSOR SAMPLING
// ════════════════════════════════════════════════════════════

void sampleSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) curTemp = t + DHT_TEMP_OFFSET;  // Koreksi kalibrasi DHT22
  if (!isnan(h)) curHum  = h;

  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  curVolt = tds.readVoltage(TDS_PIN, curTemp > -50 ? curTemp : 25.0);
  curPpm  = tds.readPPM(curVolt);

  // Disconnect TDS pin from ADC/RTC after reading
  rtc_gpio_deinit((gpio_num_t)TDS_PIN);
}

// ════════════════════════════════════════════════════════════
//  LoRa TX — Direct UART (bypass library)
// ════════════════════════════════════════════════════════════

bool waitAuxReady(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  int stableCount = 0;
  while (millis() - t0 < timeoutMs) {
    if (digitalRead(AUX_PIN) == HIGH) {
      stableCount++;
      if (stableCount >= 3) return true;
    } else {
      stableCount = 0;
    }
    delay(1);
  }
  return false;
}

bool sendLoRaDirect(const char* payload) {
  if (!waitAuxReady(2000)) {
    Serial.println("[TX] AUX timeout - E32 not ready, clearing buffer...");
    // Auto-Recovery: bersihkan buffer serial agar E32 tidak stuck
    while (e32Serial.available()) e32Serial.read();
    e32Serial.flush();
    delay(100);
    // Coba sekali lagi setelah clear buffer
    if (!waitAuxReady(1000)) {
      Serial.println("[TX] AUX still stuck after recovery");
      return false;
    }
    Serial.println("[TX] AUX recovered after buffer clear");
  }

  while (e32Serial.available()) e32Serial.read();

  e32Serial.write((uint8_t)0x00);
  e32Serial.write((uint8_t)GW_ADDL);
  e32Serial.write((uint8_t)LORA_CHAN);
  e32Serial.write((const uint8_t*)payload, strlen(payload));
  e32Serial.flush();

  delay(50);
  bool done = waitAuxReady(3000);
  return done;
}

// ════════════════════════════════════════════════════════════
//  LoRa RX — Receive commands from Gateway
// ════════════════════════════════════════════════════════════

/**
 * Check for incoming LoRa messages from Gateway.
 * Supported commands:
 *   CMD:<node>:R<n>:<0|1>     — Set relay n ON/OFF
 *   SETMODE:<node>:<0|1>      — Set mode (0=auto, 1=manual)
 *
 * Called every loop iteration.
 */
void processLoRaRx() {
  if (e32.available() <= 1) return;

  ResponseContainer rc = e32.receiveMessage();
  if (rc.status.code != 1) return;

  char buf[64];
  strncpy(buf, rc.data.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  Serial.printf("[RX] Received: %s\n", buf);

  // Parse CMD:<node>:R<n>:<state>
  if (strncmp(buf, "CMD:", 4) == 0) {
    int node = 0, rn = 0, state = 0;
    // Example: CMD:2:R1:1
    if (sscanf(buf, "CMD:%d:R%d:%d", &node, &rn, &state) == 3) {
      if (node == NODE_ADDL && rn >= 1 && rn <= RELAY_COUNT) {
        relaySet(rn, state == 1);
        Serial.printf("[CMD] Relay %d → %s (from Gateway)\n",
          rn, state ? "ON" : "OFF");
      }
    }
  }
  // Parse SETMODE:<node>:<mode>
  else if (strncmp(buf, "SETMODE:", 8) == 0) {
    int node = 0, mode = 0;
    if (sscanf(buf, "SETMODE:%d:%d", &node, &mode) == 2) {
      if (node == NODE_ADDL) {
        modeOperasi = mode;  // Simpan mode ke variabel
        Serial.printf("[MODE] Set to %s\n", mode ? "MANUAL" : "AUTO");
        // Jika beralih ke AUTO → matikan semua relay & reset dosing
        if (mode == 0) {
          relayUpdate(false, false);
          dosingActive = false;
          dosingCooldown = false;
          Serial.println("[MODE] AUTO — all relays OFF, dosing reset, setpoint aktif");
        }
      }
    }
  }
}

// ════════════════════════════════════════════════════════════
//  LCD
// ════════════════════════════════════════════════════════════

void padLine(char* buf) {
  int len = strlen(buf);
  while (len < 16) buf[len++] = ' ';
  buf[16] = '\0';
}

void updateLcd() {
  if (millis() - lcdLast < 3000) return;
  lcdLast = millis();

  char l0[17], l1[17];

  if (txTemp > -50) {
    snprintf(l0, 17, "%.1fC %.0f%% %s",
      txTemp, txHum, lastOk ? "TX:OK" : "TX:NG");
  } else {
    snprintf(l0, 17, "ERR  ERR  %s",
      lastOk ? "TX:OK" : "TX:NG");
  }

  // Line 1: TDS + relay state indicator
  snprintf(l1, 17, "TDS:%-4d %c%c #%u",
    txTds,
    relayState[0] ? '1' : '_',
    relayState[1] ? '2' : '_',
    seq);

  padLine(l0);
  padLine(l1);

  lcd.setCursor(0, 0); lcd.print(l0);
  lcd.setCursor(0, 1); lcd.print(l1);
}


void setup() {
  Serial.begin(115200);
  delay(500);

  // ── Reset reason ──
  esp_reset_reason_t reason = esp_reset_reason();
  const char* reasonStr = "UNKNOWN";
  switch (reason) {
    case ESP_RST_POWERON:   reasonStr = "POWERON"; break;
    case ESP_RST_EXT:       reasonStr = "EXT_RESET"; break;
    case ESP_RST_SW:        reasonStr = "SW_RESET"; break;
    case ESP_RST_PANIC:     reasonStr = "PANIC"; break;
    case ESP_RST_BROWNOUT:  reasonStr = "BROWNOUT"; break;
    case ESP_RST_WDT:       reasonStr = "WDT"; break;
    default: break;
  }
  Serial.printf("[BOOT] Reset: %s (%d)\n", reasonStr, reason);

  // ── Relay init (FIRST — before anything else) ──
  relayInit();

  // ── DHT22 ──
  dht.begin();
  delay(2000);

  // ── LCD ──
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Node2 Init...");

  // ── TDS Calibration ──
  tds.begin(prefs);

  // ── LoRa E32 init (library used ONLY for configuration) ──
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  delay(100);
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code != 1) {
    Serial.println("E32 config GAGAL!");
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("E32 GAGAL!");
    while (1) delay(1000);
  }
  Configuration cfg = *(Configuration*)c.data;
  cfg.ADDH = 0x00; cfg.ADDL = NODE_ADDL; cfg.CHAN = LORA_CHAN;
  cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  cfg.OPTION.fec = FEC_1_ON;
  cfg.OPTION.transmissionPower = POWER_20;
  // ════════════════════════════════════════════════════
  //  UBAH AIR DATA RATE DI SINI ◄─────────────────────
  //  (Harus sama di Gateway, Node 2, dan Node 3!)
  //
  //  Pilihan (makin kecil = makin jauh jangkauan):
  //    AIR_DATA_RATE_000_03  →   0.3 kbps  | Sangat jauh  | -138 dBm
  //    AIR_DATA_RATE_001_12  →   1.2 kbps  | Jauh         | -134 dBm ← AKTIF
  //    AIR_DATA_RATE_010_24  →   2.4 kbps  | Normal       | -131 dBm
  //    AIR_DATA_RATE_011_48  →   4.8 kbps  | Sedang       | -128 dBm
  //    AIR_DATA_RATE_100_96  →   9.6 kbps  | Dekat        | -125 dBm
  //    AIR_DATA_RATE_101_192 →  19.2 kbps  | Sangat Dekat | -121 dBm
  // ════════════════════════════════════════════════════
  cfg.SPED.airDataRate = AIR_DATA_RATE_001_12;  // ← Ubah nilai ini
  cfg.SPED.uartBaudRate = UART_BPS_9600;
  cfg.SPED.uartParity = MODE_00_8N1;
  e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
  c.close();

  // ── First sensor sample ──
  sampleSensors();
  txTemp = curTemp; txHum = curHum;
  txVolt = curVolt; txPpm = curPpm; txTds = (int)txPpm;

  // ── Relay self-test (DIMATIKAN — berbahaya untuk sistem produksi) ──
  // relaySelfTest();  // Pompa/Kipas bisa menyala sendiri saat mati lampu

  // ── Ready ──
  Serial.printf("[BOOT] AUX=%d | Node2 Ready (direct-UART + relay driver)\n",
    digitalRead(AUX_PIN));
  relayPrintStatus();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Node2 Ready");
  lcd.setCursor(0, 1); lcd.print("Relay OK");

  // ── Anti-Collision Jitter: Node 2 mulai di offset acak 0-500ms ──
  // Mencegah Node 2 dan Node 3 memancar bersamaan saat boot
  delay(random(0, 500));
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════

void loop() {
  // ── Always check for incoming LoRa commands ──
  processLoRaRx();

  if (millis() - lastSend < SEND_MS) {
    updateLcd();
    return;
  }
  lastSend = millis();
  seq++;
  txTotal++;

  // ── Build message from PREVIOUS cycle's data ──
  // Payload bersih: hanya 10 field esensial (tanpa data kalibrasi/dummy)
  char msg[100];
  snprintf(msg, sizeof(msg),
    "DATA:%d:%d:%.1f:%.1f:%d:%d:%d:%d:%lu",
    NODE_ADDL, seq, txTemp, txHum,
    (int)(txVolt * 1000), txTds,
    relayState[0] ? 1 : 0,   // R1 actual state
    relayState[1] ? 1 : 0,   // R2 actual state
    millis());

  // ── SEND via direct UART ──
  unsigned long t1 = millis();
  bool txOk = sendLoRaDirect(msg);
  unsigned long t2 = millis();

  if (!txOk) {
    Serial.println("[TX] Retry...");
    delay(500);
    txOk = sendLoRaDirect(msg);
  }

  if (!txOk) txFail++;
  lastOk = txOk;

  Serial.printf("[%d] TX:%s T:%.1f H:%.1f TDS:%d R:%d%d %lums\n",
    seq, txOk ? "OK" : "NG", txTemp, txHum, txTds,
    relayState[0], relayState[1],
    t2 - t1);

  // ── Sample for NEXT cycle ──
  sampleSensors();
  txTemp = curTemp; txHum = curHum;
  txVolt = curVolt; txPpm = curPpm; txTds = (int)curPpm;

  // ── Autonomous Control (Edge Computing) ──
  // Hanya aktif saat Mode AUTO (modeOperasi == 0)
  if (modeOperasi == 0) {
    // Setpoint Kipas Pendingin (Relay 2): Hysteresis 27°C – 30°C
    if (curTemp > TEMP_KIPAS_ON) {
      if (!relayState[1]) {
        relayOn(2);
        Serial.println("[AUTO] Kipas ON (suhu > 30°C)");
      }
    } else if (curTemp <= TEMP_KIPAS_OFF) {
      if (relayState[1]) {
        relayOff(2);
        Serial.println("[AUTO] Kipas OFF (suhu <= 27°C)");
      }
    }

    // Setpoint Pompa Nutrisi (Relay 1): Pulsed Dosing
    // PPM < 700 → Pompa ON 5 detik, lalu tunggu 10 menit
    if (dosingCooldown) {
      // Sedang menunggu nutrisi terlarut (10 menit)
      if (millis() - cooldownStart >= DOSING_COOLDOWN) {
        dosingCooldown = false;
        Serial.println("[AUTO] Cooldown 10 menit selesai, cek PPM kembali");
      }
    } else if (dosingActive) {
      // Pompa sedang menyala, cek apakah sudah 5 detik
      if (millis() - dosingStart >= DOSING_MS) {
        relayOff(1);
        dosingActive = false;
        dosingCooldown = true;
        cooldownStart = millis();
        Serial.println("[AUTO] Pompa OFF (5 dtk selesai), tunggu 10 menit");
      }
    } else if (curPpm >= 0 && curPpm < TDS_POMPA_ON) {
      // PPM kurang → mulai dosing
      relayOn(1);
      dosingActive = true;
      dosingStart = millis();
      Serial.printf("[AUTO] Pompa ON 5 dtk (PPM %.0f < 700)\n", curPpm);
    }
  }

  updateLcd();
}
