# COLLISION TEST — LoRa E32-433T20D

Pengujian Deteksi Collision 2 Node ke 1 Gateway

## TUJUAN
Analisis bagaimana jika 2 node mengirim data secara bersamaan dengan interval pengiriman yang bisa diatur (5-50ms).

---

## PERANGKAT YANG DIBUTUHKAN

- **3x ESP32 WROOM-32**
- **3x LoRa E32-433T20D**
- **2x LCD 16x2 I2C** (untuk 2 Node)
- **1x OLED 128x64 I2C SSD1306** (untuk Gateway)
- Kabel jumper dupont
- Breadboard (opsional)

---

## STRUKTUR FOLDER

```
collision_test/
├── gateway_collision/   → Upload ke ESP32 Gateway
│   └── gateway_collision.ino
├── node_collision/      → Upload ke 2x ESP32 Node (A & B)
│   └── node_collision.ino
└── README.md            → File ini
```

---

## WIRING

### GATEWAY (dengan OLED)

**LoRa E32:**
```
VCC → 3.3V  |  GND → GND
RX  → GPIO 17  |  TX → GPIO 16
AUX → GPIO 18  |  M0 → GPIO 19  |  M1 → GPIO 23
```

**OLED 128x64 I2C:**
```
VCC → 3.3V  |  GND → GND
SDA → GPIO 21  |  SCL → GPIO 22
```

### NODE A dan NODE B (dengan LCD 16x2)

**LoRa E32:**
```
VCC → 3.3V  |  GND → GND
RX  → GPIO 17  |  TX → GPIO 16
AUX → GPIO 18  |  M0 → GPIO 19  |  M1 → GPIO 23
```

**LCD 16x2 I2C:**
```
VCC → 5V  |  GND → GND
SDA → GPIO 21  |  SCL → GPIO 22
```

---

## CARA SETUP

### 1. UPLOAD GATEWAY
- Upload `gateway_collision.ino` ke ESP32 Gateway
- Biarkan running, OLED akan menampilkan status

### 2. UPLOAD NODE A
- Upload `node_collision.ino` ke ESP32 pertama
- Buka Serial Monitor (115200 baud)
- Ketik: **`ida`** (set sebagai Node A)
- ESP32 akan restart

### 3. UPLOAD NODE B
- Upload `node_collision.ino` ke ESP32 kedua
- Buka Serial Monitor (115200 baud)
- Ketik: **`idb`** (set sebagai Node B)
- ESP32 akan restart

### 4. SET INTERVAL PENGIRIMAN
Di Serial Monitor Node A atau B, ketik:
```
set 5   → interval 5ms
set 10  → interval 10ms
set 15  → interval 15ms
set 20  → interval 20ms
set 25  → interval 25ms
set 30  → interval 30ms
set 50  → interval 50ms
```

---

## PENGUJIAN

### SKENARIO 1: Kedua Node Interval Sama
```
Node A: set 10
Node B: set 10
```
→ Amati collision rate di OLED Gateway

### SKENARIO 2: Kedua Node Interval Berbeda
```
Node A: set 10
Node B: set 15
```
→ Amati apakah collision berkurang

### SKENARIO 3: Interval Sangat Pendek
```
Node A: set 5
Node B: set 5
```
→ Amati collision rate maksimal

### SKENARIO 4: Interval Lebih Panjang
```
Node A: set 50
Node B: set 50
```
→ Amati collision rate minimal

---

## DATA YANG DIKUMPULKAN

### GATEWAY (Serial Monitor)
Format CSV:
```
Time,Node,Seq,Interval(ms),RxA,LostA,RxB,LostB,Collision,Total
```

**Kolom:**
- **Time**: timestamp (ms)
- **Node**: A atau B
- **Seq**: sequence number
- **Interval**: interval TX node (ms)
- **RxA**: total paket diterima dari Node A
- **LostA**: total paket hilang dari Node A
- **RxB**: total paket diterima dari Node B
- **LostB**: total paket hilang dari Node B
- **Collision**: jumlah collision terdeteksi
- **Total**: total paket diterima (A+B)

### NODE A / B (Serial Monitor)
Format CSV:
```
Seq,Sent,Fail,LossRate(%),TxDur(ms)
```

**Kolom:**
- **Seq**: sequence number
- **Sent**: paket terkirim
- **Fail**: paket gagal kirim
- **LossRate**: % paket gagal
- **TxDur**: durasi transmisi (ms)

---

## TABEL ANALISIS

| Node A (ms) | Node B (ms) | Collision Count | Total Pkt Received | Col Rate (%) |
|-------------|-------------|-----------------|--------------------|--------------| 
| 5           | 5           |                 |                    |              |
| 10          | 10          |                 |                    |              |
| 10          | 15          |                 |                    |              |
| 15          | 15          |                 |                    |              |
| 20          | 20          |                 |                    |              |
| 25          | 25          |                 |                    |              |
| 30          | 30          |                 |                    |              |
| 50          | 50          |                 |                    |              |

**RUMUS:**
```
Collision Rate = (Collision Count / Total Packets) × 100%
```

---

## DETEKSI COLLISION

Gateway mendeteksi collision jika:
- Node A dan Node B mengirim paket
- Selisih waktu terima **< 10ms**

**Contoh:**
```
Node A paket diterima di t = 1000ms
Node B paket diterima di t = 1005ms
→ Selisih = 5ms < 10ms → COLLISION terdeteksi
```

---

## TROUBLESHOOTING

### 1. LCD/OLED tidak menyala
- Cek I2C address dengan I2C scanner
- Gateway OLED biasanya **0x3C**
- Node LCD biasanya **0x27** atau **0x3F**
- Ubah `#define LCD_ADDR` di kode jika perlu

### 2. LoRa tidak terdeteksi
- Pastikan **VCC LoRa = 3.3V** (bukan 5V!)
- Cek wiring RX/TX (silang: RX LoRa → TX ESP32)

### 3. Node tidak bisa set ID
- Pastikan Serial Monitor line ending = **"Newline"** atau **"Both NL & CR"**
- Ketik `ida` atau `idb` lalu tekan Enter

### 4. Gateway tidak terima data
- Pastikan kedua Node sudah set ID (ida/idb)
- Cek apakah LoRa semua device di channel yang sama (0x17)
- Pastikan address benar:
  - Gateway: 0x0001
  - Node A: 0x0002
  - Node B: 0x0003

---

## LIBRARY YANG DIBUTUHKAN

Pastikan library berikut sudah terinstall di Arduino IDE:

### Untuk Gateway (OLED):
- `LoRa_E32` by KrisKasprzak
- `Adafruit GFX Library`
- `Adafruit SSD1306`

### Untuk Node (LCD):
- `LoRa_E32` by KrisKasprzak
- `LiquidCrystal I2C` by Frank de Brabander

**Cara Install:**
1. Buka Arduino IDE
2. Tools → Manage Libraries
3. Search nama library di atas
4. Klik Install

---

## TIPS PENGUJIAN

1. **Mulai dari interval besar** (50ms) lalu turun bertahap ke 5ms
2. **Catat data minimal 2 menit** per skenario untuk data yang stabil
3. **Copy data CSV** dari Serial Monitor untuk analisis di Excel/Spreadsheet
4. **Amati OLED Gateway** untuk melihat collision real-time
5. **Bandingkan** collision rate saat interval sama vs berbeda

---

## HASIL YANG DIHARAPKAN

- **Interval sama**: Collision rate tinggi (>10%)
- **Interval berbeda**: Collision rate lebih rendah
- **Interval pendek (5ms)**: Collision maksimal
- **Interval panjang (50ms)**: Collision minimal

Hasil ini akan membantu menentukan **interval optimal** untuk sistem multi-node agar meminimalkan collision dan packet loss.

---

**Dibuat untuk project Hidroponik IoT**  
LoRa E32-433T20D + ESP32 WROOM-32
