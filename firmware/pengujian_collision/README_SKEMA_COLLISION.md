# 💥 PANDUAN DAN SKEMA PENGUJIAN COLLISION (TABRAKAN SINYAL LORA)

Folder ini berisi program khusus untuk menguji **Tabrakan Sinyal Radio (Collision)** antara **Node 2** dan **Node 3** secara bersamaan dengan variasi **Timing Offset (Kelipatan Selisih Waktu 0 ms s.d. 50 ms)** dan **Target 100 Paket per Batch**.

---

## 📂 Struktur Program Pengujian Collision

| Nama File / Folder | Fungsi |
|---|---|
| 📂 `node_collision_test/node_collision_test.ino` | Program Khusus **ESP32 Node 2 & Node 3** (Auto-Stop setiap 100 paket terkirim, Pengaturan Offset 0–50 ms). |
| 📂 `gateway_collision_test/gateway_collision_test.ino` | Program Khusus **ESP32 Gateway** (Penerima sinyal LoRa 433MHz, Kalkulasi Collision Loss %, Tabel Rangkuman Batch). |

---

## 📑 SKEMA PENGAMBILAN DATA FISIK (METODOLOGI SKRIPSI)

Berikut adalah skema ilmiah pengambilan data fisik yang dapat Anda masukkan ke dalam **Bab III & Bab IV Skripsi**:

### 1. Parameter Pengujian
* **Jumlah Sampel per Pengujian:** **100 Paket per Node** (Total 200 paket per Batch).
* **Variasi Kelipatan Selisih Waktu ($\Delta t$):**  
  `0 ms`, `5 ms`, `10 ms`, `15 ms`, `20 ms`, `25 ms`, `30 ms`, `35 ms`, `40 ms`, `45 ms`, `50 ms`.
* **Kondisi Pengujian:** Jarak Node 2 dan Node 3 ke Gateway dibuat konstan (misal: 5 Meter Line-of-Sight).
* **Fitur Auto-Stop:** Setelah Node mengirim paket ke-100, pengiriman **otomatis BERHENTI** agar Peneliti dapat mencatat hasil pengujian tanpa terburu-buru.

---

### 2. Langkah Kerja Pengambilan Data Fisik di Lapangan

```text
[LANGKAH 1] Set Up Hardware
  ├── Nyalakan Gateway & kedua Node (dengan Powerbank / USB).
  └── Buka Serial Monitor Gateway pada Baud Rate 115200.

[LANGKAH 2] Pengujian Batch 1 (Offset 0 ms - Dinyalakan Bersamaan)
  ├── Set offset di Node 3 = 0 ms (dinyalakan bersamaan dengan Node 2).
  ├── Kedua Node memancar dari paket #1 s.d. #100.
  ├── Pada paket ke-100, kedua Node otomatis BERHENTI (Auto-Stop).
  └── Di Gateway: Ketik 's' lalu Enter ➔ Catat hasil Tabel Batch 1 (Paket Hilang & Collision Loss %).

[LANGKAH 3] Pengujian Batch 2 s.d. 11 (Variasi Offset +5 ms s.d. +50 ms)
  ├── Pada Serial Monitor Node 3: Ketik 'offset 5' (untuk variasi 5 ms).
  ├── Tekan Tombol BOOT di ESP32 Node 2 & Node 3 secara bersamaan (atau ketik 'start').
  ├── Kedua Node akan memancar lagi dari paket #1 s.d. #100 dengan selisih waktu 5 ms.
  ├── Pada paket ke-100, Node otomatis BERHENTI kembali.
  └── Di Gateway: Ketik 's' lalu Enter ➔ Catat hasil Tabel Batch 2.

[LANGKAH 4] Ulangi untuk Kelipatan Berikutnya (+10ms, +15ms, +20ms ... +50ms)
```

---

## 📊 Contoh Format Tabel Hasil Pengujian untuk Bab IV Skripsi

Setelah pengujian selesai, Anda dapat menyusun hasil data fisik ke dalam tabel Bab IV seperti berikut:

| Batch # | Timing Offset ($\Delta t$) | Target Paket | RX Node 2 | RX Node 3 | Total Paket Hilang | Collision Loss (%) | Kategori Keandalan |
|---|---|---|---|---|---|---|---|
| **Batch 1** | **0 ms** (Tumpuk) | 200 Paket | 58 | 54 | 88 Paket | **44,0%** | Tabrakan Tinggi ⚠️ |
| **Batch 2** | **+5 ms** | 200 Paket | 75 | 78 | 47 Paket | **23,5%** | Sedang |
| **Batch 3** | **+10 ms** | 200 Paket | 88 | 91 | 21 Paket | **10,5%** | Baik |
| **Batch 4** | **+15 ms** | 200 Paket | 96 | 95 | 9 Paket | **4,5%** | Baik |
| **Batch 5** | **+20 ms** | 200 Paket | 99 | 98 | 3 Paket | **1,5%** | **Sangat Baik ✅** |
| ... | ... | ... | ... | ... | ... | ... | ... |
| **Batch 11**| **+50 ms** | 200 Paket | 100 | 100 | 0 Paket | **0,0%** | **Sangat Baik ✅** |

---

## 🛠️ Perintah Serial Monitor yang Tersedia

### Pada ESP32 Node:
* **`offset 10`** ➔ Mengatur selisih waktu pengiriman ke **+10 ms** (bisa diatur dari 0 s.d. 50 ms).
* **`start`** ➔ Memulai Batch pengujian baru (atau bisa cukup menekan **Tombol BOOT** fisik pada ESP32).

### Pada ESP32 Gateway:
* **`s`** ➔ Menampilkan **Tabel Rangkuman Hasil Batch Collision** (seperti contoh tabel Bab IV di atas).
* **`r`** ➔ Mengisi ulang / reset hitungan Batch untuk persiapan Batch berikutnya.
* **`d 5m_LOS`** ➔ Mengatur label kondisi jarak pengujian.
