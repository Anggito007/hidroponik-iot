/**
 * HydroIoT GATEWAY — WiFi + Firebase Direct
 * ============================================================
 * Terima data sensor dari Node via LoRa E32
 * Push langsung ke Firebase RTDB via HTTPS (tanpa Python bridge)
 *
 * Fitur:
 *   - LoRa RX → Firebase /sensors/nodeX (live data, overwrite)
 *   - LoRa RX → Firebase /history/nodeX (log, push setiap 60 detik)
 *   - Poll Firebase /relays/nodeX → kirim CMD relay via LoRa
 *   - Poll Firebase /mode/nodeX → kirim SETMODE via LoRa
 *   - Node watchdog (online/offline detection)
 *   - WiFi auto-reconnect
 *   - OLED display + Serial debug tetap tersedia
 *   - WiFiManager untuk setup WiFi pertama kali
 *
 * Struktur Firebase:
 *   /sensors/node2   ← live data (overwrite)
 *   /sensors/node3
 *   /history/node2   ← historical log (push)
 *   /history/node3
 *   /relays/node2    ← kontrol relay dari web
 *   /relays/node3
 *   /mode/node2      ← "auto" / "manual"
 *   /mode/node3
 *   /status/node2    ← online/offline
 *   /status/node3
 * ============================================================
 */

#include <HardwareSerial.h>
#include <LoRa_E32.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ─── Pin Definitions ────────────────────────────────────────
#define AUX_PIN   18
#define M0_PIN    21
#define M1_PIN    22
#define RX_PIN    16
#define TX_PIN    17
#define OLED_SDA   4
#define OLED_SCL   5

// ─── LoRa Config ────────────────────────────────────────────
#define GW_ADDL   0x01
#define LORA_CHAN  0x17

// ─── Firebase Config ────────────────────────────────────────
#define FIREBASE_HOST "hidroponik-server-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_URL  "https://" FIREBASE_HOST

// ─── Timing (ms) ────────────────────────────────────────────
#define OLED_INTERVAL       3000
#define RELAY_POLL_INTERVAL 3000
#define MODE_POLL_INTERVAL  5000
#define WATCHDOG_INTERVAL   10000
#define WIFI_CHECK_INTERVAL 30000
#define HISTORY_INTERVAL    60000   // Push history setiap 60 detik
#define NODE_OFFLINE_SEC    60      // Node offline setelah 60 detik

// ─── Objects ────────────────────────────────────────────────
HardwareSerial e32Serial(2);
LoRa_E32 e32(&e32Serial, AUX_PIN, M0_PIN, M1_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ─── State ──────────────────────────────────────────────────
bool wifiOk = false;
uint32_t totalRx = 0;
uint32_t fbPushOk = 0, fbPushFail = 0;

// OLED
int oledPage = 0;
unsigned long oledLast = 0;

// Serial command buffer
#define SBUF_SIZE 256
char sbuf[SBUF_SIZE];
int spos = 0;

// Timers
unsigned long lastRelayPoll = 0;
unsigned long lastModePoll  = 0;
unsigned long lastWatchdog  = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastHistory[4] = {0, 0, 0, 0};

// Per-node state
struct NodeState {
  float temp, hum;
  int tds, tdsRaw, seq;
  int r1, r2;
  uint32_t seen;       // millis() when last data received
  uint32_t nodeMillis; // millis() from node (for latency calc)
  int lastSeq;         // previous seq (for loss rate calc)
  int lostPackets;     // cumulative lost packets
  bool online;
  bool dataReady;

  // ── QoS Metrics ──
  unsigned long lastDelay;     // Delay paket terakhir (ms)
  unsigned long totalDelay;    // Akumulasi delay seluruh paket (ms)
  uint32_t totalReceived;      // Total paket diterima (untuk rata-rata)
  float avgDelay;              // Delay rata-rata (ms)
  float packetLoss;            // Packet Loss (%)
  float throughput;            // Throughput (bps)
  int payloadSize;             // Ukuran payload terakhir (byte)
  uint32_t prevSeen;           // millis() saat paket sebelumnya diterima
  char lastLost[16];           // Nomor paket hilang terakhir (string)
};
NodeState ns[4];  // index 2 and 3 used

// Relay state tracking (for change detection)
struct RelayTrack {
  int r1, r2;       // current state from Firebase
  int r1p, r2p;    // previous state
  bool initialized;
};
RelayTrack relayTrack[4];

// Mode tracking
String nodeMode[4] = {"", "", "", ""};

// ════════════════════════════════════════════════════════════
//  Firebase HTTP Helpers
// ════════════════════════════════════════════════════════════

bool firebasePut(const char* path, const String& jsonData) {
  if (!wifiOk) return false;

  WiFiClientSecure client;
  client.setInsecure();  // Skip SSL cert verification

  HTTPClient http;
  String url = String(FIREBASE_URL) + "/" + path + ".json";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int code = http.PUT(jsonData);
  http.end();

  bool ok = (code == 200);
  if (ok) fbPushOk++; else fbPushFail++;
  return ok;
}

bool firebasePost(const char* path, const String& jsonData) {
  if (!wifiOk) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(FIREBASE_URL) + "/" + path + ".json";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int code = http.POST(jsonData);  // POST = Firebase push (auto-generate key)
  http.end();

  bool ok = (code == 200);
  if (ok) fbPushOk++; else fbPushFail++;
  return ok;
}

String firebaseGet(const char* path) {
  if (!wifiOk) return "";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(FIREBASE_URL) + "/" + path + ".json";
  http.begin(client, url);
  http.setTimeout(5000);

  int code = http.GET();
  String payload = "";
  if (code == 200) {
    payload = http.getString();
  }
  http.end();
  return payload;
}

// ════════════════════════════════════════════════════════════
//  Push Sensor Data ke Firebase
// ════════════════════════════════════════════════════════════

void pushSensorToFirebase(int node) {
  if (node < 2 || node > 3) return;
  NodeState& n = ns[node];

  // Get current time
  struct tm timeinfo;
  char timeBuf[12] = "00:00:00";
  unsigned long tsMs = millis();  // fallback
  if (getLocalTime(&timeinfo, 100)) {
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
    // Epoch ms
    time_t now = mktime(&timeinfo);
    tsMs = (unsigned long)now * 1000UL;
  }

  // Build JSON for /sensors/nodeX (live, overwrite)
  // Payload berisi 8 field esensial untuk dashboard.
  // Data QoS dikirim terpisah ke /qos/nodeX.
  StaticJsonDocument<192> doc;
  doc["node"]         = node;
  doc["seq"]          = n.seq;
  doc["temp"]         = n.temp;
  doc["hum"]          = n.hum;
  doc["tds"]          = n.tds;
  doc["r1"]           = n.r1 ? true : false;
  doc["r2"]           = n.r2 ? true : false;
  doc["timestamp_ms"] = tsMs;

  // Internal-only calculations (tidak masuk Firebase, tetap dipakai)
  float lossRate = (totalRx > 0)
    ? (float)n.lostPackets / (float)(n.lostPackets + n.seq) * 100.0f
    : 0.0f;
  // lossRate, n.tdsRaw, totalRx, n.nodeMillis, timeBuf — masih tersedia di memori.

  String json;
  serializeJson(doc, json);

  // Push to /sensors/nodeX
  char path[32];
  snprintf(path, sizeof(path), "sensors/node%d", node);
  bool ok = firebasePut(path, json);

  Serial.printf("[Firebase] %s node%d: %s\n", ok ? "✓" : "✗", node,
    ok ? "OK" : "FAIL");

  // Push to /history/nodeX (setiap HISTORY_INTERVAL)
  if (millis() - lastHistory[node] >= HISTORY_INTERVAL) {
    lastHistory[node] = millis();

    StaticJsonDocument<256> hist;
    hist["temp"]         = n.temp;
    hist["hum"]          = n.hum;
    hist["tds"]          = n.tds;
    hist["timestamp_ms"] = tsMs;

    String histJson;
    serializeJson(hist, histJson);

    snprintf(path, sizeof(path), "history/node%d", node);
    bool hOk = firebasePost(path, histJson);
    Serial.printf("[History] %s node%d\n", hOk ? "✓" : "✗", node);
  }

  // Update status
  if (!n.online) {
    n.online = true;
    snprintf(path, sizeof(path), "status/node%d", node);
    StaticJsonDocument<128> statusDoc;
    statusDoc["online"]    = true;
    statusDoc["last_seen"] = timeBuf;
    String statusJson;
    serializeJson(statusDoc, statusJson);
    firebasePut(path, statusJson);
    Serial.printf("[Status] Node%d ONLINE\n", node);
  }

  // Push QoS data to /qos/nodeX
  if (n.totalReceived > 0) {
    StaticJsonDocument<256> qosDoc;
    qosDoc["delay_ms"]     = n.lastDelay;
    qosDoc["avg_delay_ms"] = (int)(n.avgDelay + 0.5f);
    qosDoc["packet_loss"]  = round(n.packetLoss * 10.0f) / 10.0f;
    qosDoc["throughput"]   = (int)(n.throughput + 0.5f);
    qosDoc["total_rx"]     = n.totalReceived;
    qosDoc["total_lost"]   = n.lostPackets;
    if (strlen(n.lastLost) > 0) {
      qosDoc["last_lost"] = n.lastLost;
    }
    qosDoc["timestamp_ms"] = tsMs;

    String qosJson;
    serializeJson(qosDoc, qosJson);

    snprintf(path, sizeof(path), "qos/node%d", node);
    bool qOk = firebasePut(path, qosJson);
    Serial.printf("[QoS] %s node%d\n", qOk ? "✓" : "✗", node);
  }
}

// ════════════════════════════════════════════════════════════
//  Process LoRa Data
// ════════════════════════════════════════════════════════════

void processLoRa(char* msg) {
  if (strncmp(msg, "DATA:", 5) != 0) return;

  char* saveptr;
  char* f = strtok_r(msg + 5, ":", &saveptr);
  int cnt = 0;
  char* fields[12];
  while (f && cnt < 11) { fields[cnt++] = f; f = strtok_r(NULL, ":", &saveptr); }
  if (cnt < 7) return;  // Minimal: node, seq, temp, hum, tdsRaw, tds, r1

  int node = atoi(fields[0]);
  if (node < 2 || node > 3) return;

  NodeState& n = ns[node];
  n.seq        = atoi(fields[1]);
  n.temp       = atof(fields[2]);
  n.hum        = atof(fields[3]);
  n.tdsRaw     = atoi(fields[4]);
  n.tds        = atoi(fields[5]);
  n.r1         = cnt >= 7 ? atoi(fields[6]) : 0;
  n.r2         = cnt >= 8 ? atoi(fields[7]) : 0;
  n.nodeMillis = cnt >= 9 ? strtoul(fields[8], NULL, 10) : 0;
  n.seen       = millis();
  n.dataReady  = true;

  // Calculate packet loss rate from sequence gaps
  if (n.lastSeq > 0 && n.seq > n.lastSeq + 1) {
    int gap = n.seq - n.lastSeq - 1;
    n.lostPackets += gap;
    // Catat nomor paket hilang terakhir
    if (gap == 1) {
      snprintf(n.lastLost, sizeof(n.lastLost), "#%d", n.lastSeq + 1);
    } else {
      snprintf(n.lastLost, sizeof(n.lastLost), "#%d-#%d", n.lastSeq + 1, n.seq - 1);
    }
    Serial.printf("[LOSS] N%d: Paket %s Hilang! (gap:%d)\n", node, n.lastLost, gap);
  }
  n.lastSeq = n.seq;

  Serial.printf("[RX] N%d #%d T:%.1f H:%.1f TDS:%d\n",
    node, n.seq, n.temp, n.hum, n.tds);

  // ── QoS Calculations ──
  // Simpan ukuran payload untuk throughput
  n.payloadSize = strlen(msg) + 5;  // +5 untuk header "DATA:" yang sudah dipotong

  // 1. Delay (Latensi)
  //    Selisih millis() Gateway saat terima vs millis() Node saat kirim
  if (n.nodeMillis > 0) {
    unsigned long gwNow = millis();
    // Jika millis Node < millis Gateway (normal karena boot berbeda),
    // hitung selisih sebagai estimasi delay transmisi
    n.lastDelay = (gwNow > n.nodeMillis) ? (gwNow - n.nodeMillis) : 0;
    n.totalDelay += n.lastDelay;
    n.totalReceived++;
    n.avgDelay = (float)n.totalDelay / (float)n.totalReceived;
  }

  // 2. Packet Loss (%)
  //    Berdasarkan gap sequence number
  if (n.seq > 0) {
    n.packetLoss = (n.lostPackets > 0)
      ? (float)n.lostPackets / (float)(n.lostPackets + n.totalReceived) * 100.0f
      : 0.0f;
  }

  // 3. Throughput (bps)
  //    Ukuran payload (bit) dibagi interval kedatangan (detik)
  if (n.prevSeen > 0 && n.seen > n.prevSeen) {
    float intervalSec = (float)(n.seen - n.prevSeen) / 1000.0f;
    if (intervalSec > 0.1f) {  // guard: minimal 100ms
      n.throughput = (float)(n.payloadSize * 8) / intervalSec;
    }
  }
  n.prevSeen = n.seen;

  Serial.printf("[QoS] N%d Delay:%lums Avg:%.0fms Loss:%.1f%% Thpt:%.0fbps\n",
    node, n.lastDelay, n.avgDelay, n.packetLoss, n.throughput);

  // Push to Firebase immediately
  if (wifiOk) {
    pushSensorToFirebase(node);
  }
}

// ════════════════════════════════════════════════════════════
//  Poll Firebase — Relay Control
// ════════════════════════════════════════════════════════════

void pollRelays() {
  for (int node = 2; node <= 3; node++) {
    // Only poll if mode is "manual"
    if (nodeMode[node] != "manual") continue;

    char path[32];
    snprintf(path, sizeof(path), "relays/node%d", node);
    String resp = firebaseGet(path);
    if (resp.length() == 0 || resp == "null") continue;

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) continue;

    RelayTrack& rt = relayTrack[node];
    int r1 = doc["r1"] ? 1 : 0;
    int r2 = doc["r2"] ? 1 : 0;

    if (!rt.initialized) {
      rt.r1p = r1; rt.r2p = r2;
      rt.initialized = true;
    }

    // Send LoRa command if state changed
    char cmd[30];
    if (r1 != rt.r1p) {
      snprintf(cmd, sizeof(cmd), "CMD:%d:R1:%d", node, r1);
      e32.sendFixedMessage(0x00, node, LORA_CHAN, cmd);
      Serial.printf("[Relay] %s\n", cmd);
      rt.r1p = r1;
    }
    if (r2 != rt.r2p) {
      snprintf(cmd, sizeof(cmd), "CMD:%d:R2:%d", node, r2);
      e32.sendFixedMessage(0x00, node, LORA_CHAN, cmd);
      Serial.printf("[Relay] %s\n", cmd);
      rt.r2p = r2;
    }
  }
}

// ════════════════════════════════════════════════════════════
//  Poll Firebase — Mode Control
// ════════════════════════════════════════════════════════════

void pollMode() {
  for (int node = 2; node <= 3; node++) {
    char path[32];
    snprintf(path, sizeof(path), "mode/node%d", node);
    String resp = firebaseGet(path);
    if (resp.length() == 0 || resp == "null") continue;

    // Remove quotes from JSON string: "auto" → auto
    resp.replace("\"", "");
    resp.trim();

    if (resp != nodeMode[node]) {
      String oldMode = nodeMode[node];
      nodeMode[node] = resp;
      Serial.printf("[Mode] Node%d: %s → %s\n", node,
        oldMode.c_str(), resp.c_str());

      // Send SETMODE to node via LoRa
      char cmd[30];
      int modeVal = (resp == "manual") ? 1 : 0;
      snprintf(cmd, sizeof(cmd), "SETMODE:%d:%d", node, modeVal);
      e32.sendFixedMessage(0x00, node, LORA_CHAN, cmd);

      // If switching to manual, reset relay tracking
      if (resp == "manual") {
        relayTrack[node].initialized = false;
      }
    }
  }
}

// ════════════════════════════════════════════════════════════
//  Node Watchdog
// ════════════════════════════════════════════════════════════

void checkWatchdog() {
  for (int node = 2; node <= 3; node++) {
    NodeState& n = ns[node];
    if (n.seen == 0) continue;  // never received data

    unsigned long ago = (millis() - n.seen) / 1000;
    if (n.online && ago > NODE_OFFLINE_SEC) {
      n.online = false;
      Serial.printf("[Watchdog] Node%d OFFLINE (%lus no data)\n", node, ago);

      // Update Firebase status
      struct tm timeinfo;
      char timeBuf[12] = "??:??:??";
      if (getLocalTime(&timeinfo, 100)) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
      }

      char path[32];
      snprintf(path, sizeof(path), "status/node%d", node);
      StaticJsonDocument<128> doc;
      doc["online"]    = false;
      doc["last_seen"] = timeBuf;
      String json;
      serializeJson(doc, json);
      firebasePut(path, json);
    }
  }
}

// ════════════════════════════════════════════════════════════
//  WiFi Management
// ════════════════════════════════════════════════════════════

void checkWifi() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !wifiOk) {
    wifiOk = true;
    Serial.printf("[WiFi] Reconnected: %s IP:%s\n",
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else if (!connected && wifiOk) {
    wifiOk = false;
    Serial.println("[WiFi] Disconnected — will auto-reconnect");
    WiFi.reconnect();
  }
}

// ════════════════════════════════════════════════════════════
//  Serial Commands (for debug when USB connected)
// ════════════════════════════════════════════════════════════

void processLine(const char* line) {
  char lower[64];
  strncpy(lower, line, 63); lower[63] = 0;
  for (char* p = lower; *p; p++) *p = tolower(*p);

  if (strcmp(lower, "help") == 0) {
    Serial.println("help | status | wifi | firebase | r1/r2 <node> on/off");
  }
  else if (strcmp(lower, "status") == 0) {
    for (int n = 2; n <= 3; n++) {
      unsigned long ago = ns[n].seen ? (millis() - ns[n].seen) / 1000 : 9999;
      Serial.printf("N%d: T:%.1f H:%.1f TDS:%d #%d %s (%lus ago)\n",
        n, ns[n].temp, ns[n].hum, ns[n].tds, ns[n].seq,
        ns[n].online ? "ONLINE" : "OFFLINE", ago);
    }
  }
  else if (strcmp(lower, "wifi") == 0) {
    if (wifiOk)
      Serial.printf("WiFi: %s IP:%s RSSI:%d\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else
      Serial.println("WiFi: Disconnected");
  }
  else if (strcmp(lower, "firebase") == 0) {
    Serial.printf("Firebase: OK:%lu FAIL:%lu\n", fbPushOk, fbPushFail);
  }
  else if (lower[0] == 'r' && lower[1] >= '1' && lower[1] <= '2') {
    int rn = lower[1] - '0';
    bool on = (strstr(lower, "on") != NULL);
    int nd = -1;
    if (strstr(lower, "2")) nd = 2; else if (strstr(lower, "3")) nd = 3;
    if (nd > 0) {
      char cmd[30];
      snprintf(cmd, sizeof(cmd), "CMD:%d:R%d:%d", nd, rn, on ? 1 : 0);
      e32.sendFixedMessage(0x00, nd, LORA_CHAN, cmd);
      Serial.printf("[TX] %s\n", cmd);
    }
  }
}

// ════════════════════════════════════════════════════════════
//  OLED Display
// ════════════════════════════════════════════════════════════

void updateOled() {
  oled.clearBuffer();
  char b[22];

  switch (oledPage) {
    case 0: {
      oled.setFont(u8g2_font_helvB10_tr);
      oled.drawStr(5, 12, "= GW =");
      oled.setFont(u8g2_font_helvR08_tr);
      snprintf(b, sizeof(b), "WiFi:%s", wifiOk ? "OK" : "OFF");
      oled.drawStr(0, 28, b);
      snprintf(b, sizeof(b), "RX:%lu FB:%lu/%lu", totalRx, fbPushOk, fbPushFail);
      oled.drawStr(0, 38, b);
      for (int n = 2; n <= 3; n++) {
        unsigned long ago = ns[n].seen ? (millis() - ns[n].seen) / 1000 : 9999;
        snprintf(b, sizeof(b), "N%d:%s %lus", n, ago < 30 ? "ON" : "OFF",
          ago < 9999 ? ago : 0);
        oled.drawStr(0, 44 + (n - 2) * 14, b);
      }
      break;
    }
    case 1:
    case 2: {
      int n = oledPage + 1;
      oled.setFont(u8g2_font_helvR08_tr);
      snprintf(b, sizeof(b), "-- Node %d --", n);
      oled.drawStr(1, 10, b);
      if (ns[n].seen) {
        snprintf(b, sizeof(b), "T:%.1fC H:%.0f%%", ns[n].temp, ns[n].hum);
        oled.drawStr(0, 24, b);
        snprintf(b, sizeof(b), "TDS:%dppm", ns[n].tds);
        oled.drawStr(0, 36, b);
        snprintf(b, sizeof(b), "#%d %lus ago", ns[n].seq,
          (unsigned long)(millis() - ns[n].seen) / 1000);
        oled.drawStr(0, 48, b);
      } else {
        oled.drawStr(10, 35, "No data");
      }
      break;
    }
    case 3:
    case 4: {
      // ── QoS Pages: page 3 = Node 2, page 4 = Node 3 ──
      int n = (oledPage == 3) ? 2 : 3;
      oled.setFont(u8g2_font_helvR08_tr);
      snprintf(b, sizeof(b), "-- QoS Node %d --", n);
      oled.drawStr(1, 10, b);
      if (ns[n].totalReceived > 0) {
        snprintf(b, sizeof(b), "Delay:%lums", ns[n].lastDelay);
        oled.drawStr(0, 24, b);
        snprintf(b, sizeof(b), "Avg  :%.0fms", ns[n].avgDelay);
        oled.drawStr(0, 34, b);
        snprintf(b, sizeof(b), "Loss :%.1f%%", ns[n].packetLoss);
        oled.drawStr(0, 44, b);
        snprintf(b, sizeof(b), "Thpt :%dbps", (int)ns[n].throughput);
        oled.drawStr(0, 54, b);
      } else {
        oled.drawStr(10, 35, "No QoS data");
      }
      break;
    }
  }
  oled.sendBuffer();
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("  HydroIoT Gateway — Firebase Direct");
  Serial.println("========================================");

  // Init state
  memset(ns, 0, sizeof(ns));
  memset(relayTrack, 0, sizeof(relayTrack));
  for (int i = 0; i < 4; i++) {
    ns[i].temp = -99; ns[i].hum = -99;
  }

  // OLED
  oled.begin();
  oled.setContrast(200);
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.drawStr(10, 12, "HydroIoT GW");
  oled.setFont(u8g2_font_helvR08_tr);
  oled.drawStr(10, 30, "Init LoRa...");
  oled.sendBuffer();

  // E32 LoRa init
  pinMode(M0_PIN, OUTPUT); digitalWrite(M0_PIN, LOW);
  pinMode(M1_PIN, OUTPUT); digitalWrite(M1_PIN, LOW);
  delay(100);
  e32Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  e32.begin();

  ResponseStructContainer c = e32.getConfiguration();
  if (c.status.code != 1) {
    Serial.println("[ERROR] E32 config gagal!");
    oled.clearBuffer();
    oled.drawStr(10, 30, "E32 GAGAL!");
    oled.sendBuffer();
    while (1) delay(1000);
  }
  Configuration cfg = *(Configuration*)c.data;
  cfg.ADDH = 0x00; cfg.ADDL = GW_ADDL; cfg.CHAN = LORA_CHAN;
  cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  cfg.OPTION.fec = FEC_1_ON;
  cfg.OPTION.transmissionPower = POWER_20;
  cfg.SPED.airDataRate = AIR_DATA_RATE_010_24;
  cfg.SPED.uartBaudRate = UART_BPS_9600;
  cfg.SPED.uartParity = MODE_00_8N1;
  e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
  c.close();
  Serial.println("[LoRa] E32 OK");

  // WiFi
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.drawStr(10, 12, "HydroIoT GW");
  oled.setFont(u8g2_font_helvR08_tr);
  oled.drawStr(10, 30, "Connecting WiFi");
  oled.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  if (wm.autoConnect("HydroIoT-Gateway")) {
    wifiOk = true;
    Serial.printf("[WiFi] Connected: %s IP:%s\n",
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    // Sync time via NTP (untuk timestamp)
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("[NTP] Syncing time (WIB +7)...");
  } else {
    Serial.println("[WiFi] Not connected — LoRa-only mode");
  }

  // Ready
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tr);
  oled.drawStr(10, 12, "GW Ready");
  oled.setFont(u8g2_font_helvR08_tr);
  oled.drawStr(10, 30, wifiOk ? "WiFi+Firebase" : "LoRa Only");
  oled.sendBuffer();

  Serial.printf("[BOOT] Gateway Ready | WiFi:%s\n", wifiOk ? "OK" : "OFF");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // ── 1. Serial commands (non-blocking) ──
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (spos > 0) { sbuf[spos] = 0; processLine(sbuf); spos = 0; }
    } else if (spos < SBUF_SIZE - 1) {
      sbuf[spos++] = ch;
    }
  }

  // ── 2. LoRa RX ──
  if (e32.available() > 1) {
    ResponseContainer rc = e32.receiveMessage();
    if (rc.status.code == 1) {
      totalRx++;
      char buf[160];
      strncpy(buf, rc.data.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      processLoRa(buf);
    }
  }

  // ── 3. OLED update ──
  if (now - oledLast >= OLED_INTERVAL) {
    oledLast = now;
    updateOled();
    oledPage = (oledPage + 1) % 5;  // 5 halaman: Status, N2, N3, QoS N2, QoS N3
  }

  // ── 4. Firebase polling (only if WiFi connected) ──
  if (wifiOk) {
    // Poll relay states
    if (now - lastRelayPoll >= RELAY_POLL_INTERVAL) {
      lastRelayPoll = now;
      pollRelays();
    }

    // Poll mode
    if (now - lastModePoll >= MODE_POLL_INTERVAL) {
      lastModePoll = now;
      pollMode();
    }

    // Node watchdog
    if (now - lastWatchdog >= WATCHDOG_INTERVAL) {
      lastWatchdog = now;
      checkWatchdog();
    }
  }

  // ── 5. WiFi health check ──
  if (now - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
    lastWifiCheck = now;
    checkWifi();
  }
}
