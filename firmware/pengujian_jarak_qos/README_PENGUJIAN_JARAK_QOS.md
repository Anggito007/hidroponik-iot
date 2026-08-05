# 🛰️ PANDUAN PENGUJIAN JARAK, QoS & SENSOR (PROGRAM KHUSUS)

Program ini dibuat **KHUSUS untuk Pengujian Independen (Jarak, QoS, Sensor, dan Relay)**.
> ⚠️ **PENTING:** Program ini berada di folder terpisah dan **TIDAK MENGANGGU kodingan di `Program Utama` Anda 100% aman.**

---

## 📂 Struktur File Program Pengujian

| Nama File / Folder | Fungsi |
|---|---|
| 📂 `node_test/node_test.ino` | Program Khusus **ESP32 Node 2 / Node 3** (Tanpa WiFi, Pengiriman LoRa Cepat P2P, Baca DHT22 & TDS, Kontrol Relay). |
| 📂 `gateway_test/gateway_test.ino` | Program Khusus **ESP32 Gateway** (Tanpa WiFi/Firebase HTTP, Latensi Murni 0ms, Hitung QoS TIPHON, Tampilan OLED & Serial). |
| 🐍 `export_pdf_note.py` | Script Python untuk merekap hasil Serial Monitor dan otomatis mengonversi ke **Laporan PDF / Catatan Pengujian**. |

---

## 🚀 Langkah Cara Penggunaan

### 1. Upload Program Pengujian
1. **Upload ke ESP32 Node 2 / 3:**  
   Buka Arduino IDE ➔ Buka file `firmware/pengujian_jarak_qos/node_test/node_test.ino` ➔ Upload ke ESP32 Node.  
   *(Catatan: Untuk Node 3, ubah `#define NODE_ADDL 0x02` menjadi `0x03`).*

2. **Upload ke ESP32 Gateway:**  
   Buka Arduino IDE ➔ Buka file `firmware/pengujian_jarak_qos/gateway_test/gateway_test.ino` ➔ Upload ke ESP32 Gateway.

---

### 2. Melakukan Pengujian Jarak & QoS (Via Serial Monitor)
Buka Serial Monitor Gateway pada Baud Rate **`115200`**. Anda dapat menggunakan fitur interaktif berikut:

* **Mengubah Penanda Jarak:**  
  Ketik `d 10m` ➔ Mengatur penanda jarak pengujian saat ini ke **10 Meter**.  
  Ketik `d 50m_NLOS` ➔ Mengatur penanda pengujian ke **50 Meter (Beda Ruangan/Terhalang Tembok)**.
* **Melihat Tabel Rangkuman TIPHON:**  
  Ketik `s` ➔ Menampilkan tabel QoS lengkap (Total RX, Packet Loss %, Delay ms, Throughput bps, & Kategori TIPHON).
* **Reset Hitungan QoS:**  
  Ketik `r` ➔ Mengosongkan akumulasi QoS untuk memulai sesi pengujian jarak baru.
* **Uji Coba Relay dari Gateway:**  
  Ketik `c2r1` ➔ Menyalakan Pompa Nutrisi Node 2 via LoRa.  
  Ketik `c2r0` ➔ Mematikan Pompa Nutrisi Node 2 via LoRa.

---

### 3. Mengonversi Hasil Pengujian ke Laporan PDF & Catatan
1. Copy seluruh teks log dari Serial Monitor Gateway.
2. Paste ke dalam file teks baru bernama `catatan_log_pengujian.txt` di folder ini.
3. Buka Terminal / Command Prompt dan jalankan script Python:
   ```bash
   python export_pdf_note.py catatan_log_pengujian.txt
   ```
4. Script akan otomatis menghasilkan file `laporan_pengujian_jarak_qos.html`.
5. Buka file HTML tersebut di browser (Chrome / Edge), lalu tekan **Ctrl + P** ➔ Simpan sebagai **PDF**.

---

### 💡 Keunggulan Program Pengujian Ini:
1. **0ms Latensi Murni:** Karena tidak ada koneksi HTTP Firebase yang memblokir, pengiriman LoRa berjalan secara instan dan murni (Delay asli ~115ms - 130ms).
2. **Tidak Merusak Program Utama:** Setelah selesai pengujian, Anda tinggal meng-upload kembali `Program Utama` ke ESP32 Anda tanpa ada perubahan apa pun di program utama.
