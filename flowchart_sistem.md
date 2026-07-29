# Flowchart Sistem HydroIoT
## Format Mermaid Diagram

---

## 1. FLOWCHART UTAMA SISTEM

```mermaid
flowchart TD
    START([START]) --> INIT_N1 & INIT_N2 & INIT_GW

    INIT_N1[Inisialisasi Node 1\nADDL=0x02\nRelay OFF\nDHT22 warmup\nBaca K-factor NVS\nKonfigurasi LoRa E32] --> LOOP_N1([Loop Node 1])

    INIT_N2[Inisialisasi Node 2\nADDL=0x03\nRelay OFF\nDHT22 warmup\nBaca K-factor NVS\nKonfigurasi LoRa E32] --> LOOP_N2([Loop Node 2])

    INIT_GW[Inisialisasi Gateway\nADDL=0x01\nKonfigurasi LoRa E32\nKoneksi WiFi\nSync NTP\nInit Firebase] --> LOOP_GW([Loop Gateway])
```

---

## 2. FLOWCHART LOOP NODE

```mermaid
flowchart TD
    LOOP([Loop Node]) --> CEK_LORA{Ada pesan\nLoRa masuk?}

    CEK_LORA -->|Ya| HANDLE[handleCommand\nCMD / CAL / CALRESET]
    CEK_LORA -->|Tidak| CEK_TIMER
    HANDLE --> CEK_TIMER

    CEK_TIMER{Sudah 7 detik\nsejak kirim terakhir?} -->|Tidak| LOOP
    CEK_TIMER -->|Ya| BACA_DHT

    BACA_DHT[Baca DHT22\nmax 10 retry\n→ temp, hum] --> BACA_TDS
    BACA_TDS[Baca TDS Sensor\n15x ADC rata-rata\n→ tdsRaw, tdsCal] --> CEK_MODE

    CEK_MODE{Mode\nAktif?} -->|MANUAL| MODE_MANUAL
    CEK_MODE -->|AUTO| MODE_AUTO

    MODE_MANUAL[Relay dikontrol\ndari Web Dashboard\nvia Firebase] --> CEK_TIMEOUT

    CEK_TIMEOUT{Sudah 10 menit\nsejak perintah\nmanual terakhir?} -->|Ya| BACK_AUTO[Reset manualRelay=false\nKembali ke mode AUTO]
    CEK_TIMEOUT -->|Tidak| KIRIM
    BACK_AUTO --> MODE_AUTO

    MODE_AUTO[Auto-Control Relay] --> CEK_MAN1

    CEK_MAN1{R1: manualRelay\n= false?} -->|Ya| CEK_R1
    CEK_MAN1 -->|Tidak| CEK_MAN2
    CEK_R1{TDS < 600ppm?} -->|Ya| R1_ON[Pompa Nutrisi ON]
    CEK_R1 -->|Tidak| CEK_R1B
    CEK_R1B{TDS > 650ppm?} -->|Ya| R1_OFF[Pompa Nutrisi OFF]
    CEK_R1B -->|Tidak| CEK_MAN2
    R1_ON --> CEK_MAN2
    R1_OFF --> CEK_MAN2

    CEK_MAN2{R2: manualRelay\n= false?} -->|Ya| CEK_R2
    CEK_MAN2 -->|Tidak| CEK_MAN3
    CEK_R2{Suhu > 30°C?} -->|Ya| R2_ON[Kipas ON]
    CEK_R2 -->|Tidak| CEK_R2B
    CEK_R2B{Suhu < 28°C?} -->|Ya| R2_OFF[Kipas OFF]
    CEK_R2B -->|Tidak| CEK_MAN3
    R2_ON --> CEK_MAN3
    R2_OFF --> CEK_MAN3

    CEK_MAN3 -->|Tidak| KIRIM

    KIRIM[Susun paket DATA\nDATA:node:seq:temp:hum:\ntdsRaw:tdsCal:\nr1:r2:r3:kfactor] --> TX

    TX[Kirim via LoRa\nke Gateway ADDL=0x01] --> CEK_TX
    CEK_TX{Berhasil?} -->|Ya| OK[totalSent++]
    CEK_TX -->|Tidak| FAIL[totalFail++]
    OK --> LOOP
    FAIL --> LOOP
```

---

## 3. FLOWCHART LOOP GATEWAY

```mermaid
flowchart TD
    LOOP_GW([Loop Gateway]) --> CEK_WIFI{WiFi\nterhubung?}

    CEK_WIFI -->|Tidak| RECONNECT[Reconnect WiFi\ncoba tiap 5 detik]
    RECONNECT --> CEK_LORA_GW
    CEK_WIFI -->|Ya| CEK_LORA_GW

    CEK_LORA_GW{Ada data LoRa\ndari Node?} -->|Ya| PARSE
    CEK_LORA_GW -->|Tidak| CEK_POLL

    PARSE[Parse DATA\nnode, seq, temp, hum\ntds, r1, r2, r3] --> LOSS
    LOSS[Hitung Packet Loss\ndari gap seq] --> CEK_ONLINE
    CEK_ONLINE{Node sebelumnya\nOFFLINE?} -->|Ya| SET_ONLINE[Set online=true\nPush /status/nodeX]
    CEK_ONLINE -->|Tidak| PUSH
    SET_ONLINE --> PUSH
    PUSH[Push ke Firebase\n/sensors/nodeX\ntermasuk loss_rate] --> CEK_POLL

    CEK_POLL{Sudah 2 detik\nsejak poll relay?} -->|Tidak| WATCHDOG
    CEK_POLL -->|Ya| POLL

    POLL[Baca Firebase\n/relays/node2\n/relays/node3] --> CEK_CHANGE
    CEK_CHANGE{Ada perubahan\nr1 / r2 / r3?} -->|Ya| SEND_CMD
    CEK_CHANGE -->|Tidak| WATCHDOG
    SEND_CMD[Kirim CMD via LoRa\nCMD:node:Rx:0 atau 1\nke Node tujuan] --> WATCHDOG

    WATCHDOG{Cek tiap 5 detik\nNode tidak kirim\ndata > 30 detik?} -->|Ya| SET_OFFLINE
    WATCHDOG -->|Tidak| LOOP_GW
    SET_OFFLINE[Tandai OFFLINE\nPush /status/nodeX\nonline=false] --> LOOP_GW
```

---

## 4. FLOWCHART KONTROL RELAY DARI WEB

```mermaid
flowchart TD
    USER([User buka\nWeb Dashboard]) --> LISTEN[Firebase onValue listener\nupdate UI realtime]

    LISTEN --> CEK_TOGGLE{User klik\ntoggle relay?}
    CEK_TOGGLE -->|Tidak| LISTEN
    CEK_TOGGLE -->|Ya| CEK_MODE

    CEK_MODE{Mode\nMANUAL aktif?} -->|Tidak| BLOCKED[Toggle diblokir\nMode AUTO aktif]
    BLOCKED --> LISTEN
    CEK_MODE -->|Ya| SET_FB

    SET_FB[Tulis ke Firebase\n/relays/nodeX/rX\ntrue atau false] --> GW_POLL

    GW_POLL[Gateway poll Firebase\ntiap 2 detik] --> DETECT{Ada perubahan\nrelay?}
    DETECT -->|Tidak| GW_POLL
    DETECT -->|Ya| SEND[Kirim CMD via LoRa\nCMD:node:Rx:0 atau 1]

    SEND --> NODE_RX[Node terima CMD]
    NODE_RX --> SET_RELAY[setRelay\nmanualRelay=true\nmanualTime=millis]
    SET_RELAY --> RELAY_FISIK[Relay fisik\nON atau OFF]

    RELAY_FISIK --> TIMEOUT{Sudah 10 menit\nsejak manual?}
    TIMEOUT -->|Tidak| TIMEOUT
    TIMEOUT -->|Ya| AUTO_BACK[manualRelay=false\nAuto-control\naktif kembali]
    AUTO_BACK --> LISTEN
```

---

## 5. FLOWCHART HANDLECOMMAND NODE

```mermaid
flowchart TD
    HC([handleCommand]) --> CEK_CMD{startsWith\nCMD:?}

    CEK_CMD -->|Ya| PARSE_CMD[Parse node, Rx, state]
    PARSE_CMD --> CEK_NODE_CMD{Node ID\ncocok?}
    CEK_NODE_CMD -->|Tidak| END1([return])
    CEK_NODE_CMD -->|Ya| SET_RELAY_CMD[setRelay\nmanualRelay=false\nlalu set val]
    SET_RELAY_CMD --> END1

    CEK_CMD -->|Tidak| CEK_CAL{startsWith\nCAL:?}
    CEK_CAL -->|Ya| PARSE_CAL[Parse node, ppm]
    PARSE_CAL --> CEK_NODE_CAL{Node ID\ncocok?}
    CEK_NODE_CAL -->|Tidak| END2([return])
    CEK_NODE_CAL -->|Ya| HITUNG_K[Baca TDS raw\nK = ppm / raw]
    HITUNG_K --> SIMPAN_K[Simpan K ke NVS\nFlash ESP32]
    SIMPAN_K --> KIRIM_CALOK[Kirim CALOK\nke Gateway]
    KIRIM_CALOK --> END2

    CEK_CAL -->|Tidak| CEK_RESET{startsWith\nCALRESET:?}
    CEK_RESET -->|Tidak| END3([return])
    CEK_RESET -->|Ya| PARSE_RESET[Parse node ID]
    PARSE_RESET --> CEK_NODE_RESET{Node ID\ncocok?}
    CEK_NODE_RESET -->|Tidak| END3
    CEK_NODE_RESET -->|Ya| RESET_K[K = 1.0\nSimpan ke NVS]
    RESET_K --> END3
```

---

## 6. FLOWCHART RINGKASAN SISTEM

```mermaid
flowchart LR
    subgraph NODE1[Node 1 - Hidroponik A]
        S1[DHT22\nHC-SR04\nTDS] --> AC1[Auto-Control\nRelay]
        AC1 --> TX1[Kirim DATA\nLoRa]
        RX1[Terima CMD\nLoRa] --> RL1[Pompa Air\nPompa Nutrisi\nKipas]
    end

    subgraph NODE2[Node 2 - Hidroponik B]
        S2[DHT22\nHC-SR04\nTDS] --> AC2[Auto-Control\nRelay]
        AC2 --> TX2[Kirim DATA\nLoRa]
        RX2[Terima CMD\nLoRa] --> RL2[Pompa Air\nPompa Nutrisi\nKipas]
    end

    subgraph GW[Gateway]
        LORA_RX[Terima LoRa\nHitung Loss Rate] --> FB_PUSH[Push Firebase\n/sensors/nodeX]
        FB_POLL[Poll Firebase\n/relays/nodeX] --> LORA_TX[Kirim CMD\nLoRa]
    end

    subgraph CLOUD[Firebase Realtime Database]
        DB_S[/sensors/node2\n/sensors/node3\ntermasuk loss_rate]
        DB_R[/relays/node2\n/relays/node3]
        DB_ST[/status/node2\n/status/node3]
    end

    subgraph WEB[Web Dashboard]
        MON[Monitor Sensor\nPacket Loss]
        CTRL[Kontrol Relay\nMode Auto/Manual]
    end

    TX1 -->|LoRa 433MHz| LORA_RX
    TX2 -->|LoRa 433MHz| LORA_RX
    LORA_TX -->|LoRa 433MHz| RX1
    LORA_TX -->|LoRa 433MHz| RX2

    FB_PUSH -->|WiFi| DB_S
    FB_POLL -->|WiFi| DB_R

    DB_S -->|Realtime| MON
    CTRL -->|set| DB_R
```

---

## CARA RENDER

### Mermaid Live Editor (Paling Mudah)
1. Buka [mermaid.live](https://mermaid.live)
2. Copy kode di dalam blok mermaid
3. Paste ke editor kiri → diagram muncul di kanan
4. Export PNG atau SVG untuk laporan skripsi

### VS Code
1. Install extension **Markdown Preview Mermaid Support**
2. Buka file ini
3. Tekan `Ctrl+Shift+V`
