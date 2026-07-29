"""
HydroIoT LoRa Bridge + Firebase
Serial (Gateway ESP32) --> Firebase Realtime Database

Tugas:
  - Terima data sensor dari Gateway → push ke Firebase /sensors/nodeX
  - Poll Firebase /relays/nodeX → kirim CMD relay ke Gateway via Serial
    (hanya saat mode MANUAL)
  - Poll Firebase /mode/nodeX → kirim AUTORESET saat pindah ke AUTO
  - Watchdog: deteksi node offline → update Firebase /status/nodeX
  - Auto-reconnect Serial jika terputus

Struktur Firebase:
  /sensors/node2   ← data sensor Node 2 (Hidroponik A)
  /sensors/node3   ← data sensor Node 3 (Hidroponik B)
  /relays/node2    ← kontrol relay Node 2 dari web dashboard
  /relays/node3    ← kontrol relay Node 3 dari web dashboard
  /status/node2    ← status online/offline Node 2
  /status/node3    ← status online/offline Node 3
  /mode/node2      ← mode kontrol: "auto" atau "manual"
  /mode/node3      ← mode kontrol: "auto" atau "manual"

Kalibrasi TDS dilakukan TERPISAH via Serial Monitor (perintah CAL).
"""

import asyncio
import serial
import json
import requests
from datetime import datetime

# ============================================================
#  KONFIGURASI
# ============================================================

SERIAL_PORT          = "COM3"    # Port USB Gateway ESP32 (cek Device Manager)
BAUD_RATE            = 115200
FIREBASE_URL         = "https://hidroponik-server-default-rtdb.asia-southeast1.firebasedatabase.app"
FIREBASE_AUTH        = ""        # Kosong = Firebase Test Mode
NODES                = [2, 3]    # ADDL node aktif (2=Hidroponik A, 3=Hidroponik B)

RELAY_POLL_INTERVAL  = 0.3       # detik, interval poll Firebase untuk relay
NODE_OFFLINE_TIMEOUT = 60        # detik, node dianggap offline jika tidak ada data
MODE_POLL_INTERVAL   = 1.0       # detik, interval poll Firebase untuk mode

# ============================================================
#  Firebase Helper
# ============================================================

def fb_url(path):
    url = f"{FIREBASE_URL}/{path}.json"
    if FIREBASE_AUTH:
        url += f"?auth={FIREBASE_AUTH}"
    return url

def firebase_put(path, data):
    try:
        r = requests.put(fb_url(path), json=data, timeout=5)
        if r.status_code != 200:
            print(f"[Firebase] ERROR {r.status_code}: {r.text[:100]}")
    except Exception as e:
        print(f"[Firebase] PUT error: {e}")

def firebase_get(path):
    try:
        r = requests.get(fb_url(path), timeout=5)
        if r.status_code == 200:
            return r.json()
        return None
    except Exception as e:
        print(f"[Firebase] GET error: {e}")
        return None

# ============================================================
#  State Global
# ============================================================

ser = None

# State relay terakhir per node (untuk deteksi perubahan)
relay_last = {
    2: {"r1": None, "r2": None},
    3: {"r1": None, "r2": None},
}

# Mode kontrol per node: "auto" atau "manual"
mode_last = {2: "auto", 3: "auto"}

# Waktu data terakhir diterima per node (untuk watchdog)
node_last_rx = {2: None, 3: None}
node_online  = {2: False, 3: False}

# ============================================================
#  Kirim CMD Relay ke Gateway via Serial
# ============================================================

async def send_relay_cmd(node: int, key: str, state: bool):
    """Kirim perintah relay ke Gateway dalam format JSON."""
    if ser and ser.is_open:
        payload = json.dumps({
            "type":  "cmd",
            "node":  node,
            "relay": key.upper(),
            "state": 1 if state else 0
        })
        try:
            ser.write((payload + "\n").encode())
            print(f"[CMD] Node{node} {key.upper()} → {'ON' if state else 'OFF'}")
        except serial.SerialException as e:
            print(f"[CMD] Gagal kirim: {e}")
    else:
        print(f"[CMD] Serial tidak terhubung, perintah diabaikan")

async def send_setmode(node: int, mode: str):
    """Kirim perintah SETMODE ke Gateway → Node untuk set mode AUTO/MANUAL.
    mode: 'auto' atau 'manual'
    """
    if ser and ser.is_open:
        mode_val = 1 if mode == "manual" else 0
        payload = json.dumps({
            "type":  "setmode",
            "node":  node,
            "mode":  mode_val
        })
        try:
            ser.write((payload + "\n").encode())
            print(f"[MODE] Node{node} → SETMODE:{mode.upper()} dikirim")
        except serial.SerialException as e:
            print(f"[MODE] Gagal kirim: {e}")
    else:
        print(f"[MODE] Serial tidak terhubung, perintah diabaikan")

# Backward compat alias
async def send_autoreset(node: int):
    await send_setmode(node, "auto")

# ============================================================
#  Push Data Sensor ke Firebase
# ============================================================

async def push_sensor(data: dict):
    node = data.get("node")
    if node not in NODES:
        print(f"[WARN] Data dari node tidak dikenal: {node}")
        return

    payload = {
        "node":       node,
        "seq":        data.get("seq"),
        "temp":       data.get("temp"),
        "hum":        data.get("hum"),
        "tds":        data.get("tds"),
        "tds_raw":    data.get("tds_raw"),
        "tds_min":    data.get("tds_min", 600),
        "tds_max":    data.get("tds_max", 800),
        "rssi":       data.get("rssi", 0),
        "loss_rate":  round(data.get("loss_rate", 0.0), 1),
        "total_rx":   data.get("total_rx", 0),
        "tx_time":    data.get("tx_time", 0),
        "gw_time":    data.get("gw_time", 0),
        "r1":         bool(data.get("r1", 0)),
        "r2":         bool(data.get("r2", 0)),
        "timestamp":  datetime.now().strftime("%H:%M:%S"),
        "timestamp_ms": int(datetime.now().timestamp() * 1000),
    }
    await asyncio.to_thread(firebase_put, f"sensors/node{node}", payload)

    # Update status online jika sebelumnya offline
    node_last_rx[node] = asyncio.get_event_loop().time()
    if not node_online[node]:
        node_online[node] = True
        await asyncio.to_thread(firebase_put, f"status/node{node}", {
            "online":    True,
            "last_seen": datetime.now().strftime("%H:%M:%S")
        })
        print(f"[STATUS] Node{node} ONLINE")

    print(f"[Firebase] ✓ Node{node} | "
          f"T:{payload['temp']}°C H:{payload['hum']}% "
          f"TDS:{payload['tds']}ppm "
          f"R:{int(payload['r1'])}{int(payload['r2'])}")

# ============================================================
#  Handle JSON dari Gateway
# ============================================================

async def handle_gateway_json(data: dict):
    t = data.get("type", "")

    if t == "data":
        await push_sensor(data)

    elif t == "status":
        print(f"[GW] {data.get('msg', '')}")

    elif t == "autoreset_ok":
        node = data.get("node")
        print(f"[AUTORESET] ✓ Node{node} auto-control aktif kembali")

    elif t == "setmode_ok":
        node = data.get("node")
        mode = data.get("mode")
        print(f"[SETMODE] ✓ Node{node} mode → {'MANUAL' if mode == 1 else 'AUTO'} dikonfirmasi")

    elif t == "cmd_echo":
        node  = data.get("node")
        relay = data.get("relay")
        state = data.get("state")
        ok    = data.get("ok", False)
        print(f"[CMD ECHO] Node{node} {relay} → {'ON' if state else 'OFF'} "
              f"{'✓' if ok else '✗ GAGAL'}")

    elif t == "node_offline":
        node = data.get("node")
        print(f"[WARN] Node{node} OFFLINE (dilaporkan Gateway)")
        node_online[node] = False
        await asyncio.to_thread(firebase_put, f"status/node{node}", {
            "online":    False,
            "last_seen": datetime.now().strftime("%H:%M:%S")
        })

    else:
        print(f"[GW] {data}")

# ============================================================
#  Serial Reader — Terima data dari Gateway
# ============================================================

async def serial_reader():
    global ser
    print("[SERIAL] Menghubungkan ke Gateway...")

    while True:
        # Koneksi / reconnect
        if ser is None or not ser.is_open:
            try:
                ser = serial.Serial(
                    SERIAL_PORT, BAUD_RATE,
                    timeout=1, dsrdtr=False, rtscts=False
                )
                ser.dtr = False
                ser.rts = False
                print(f"[SERIAL] ✓ Terhubung ke {SERIAL_PORT}")
            except serial.SerialException as e:
                print(f"[SERIAL] Gagal: {e} — coba lagi 5 detik...")
                ser = None
                await asyncio.sleep(5)
                continue

        # Baca data dari Gateway
        try:
            if ser.in_waiting > 0:
                raw = ser.readline().decode("utf-8", errors="ignore").strip()
                if not raw:
                    continue
                try:
                    data = json.loads(raw)
                    await handle_gateway_json(data)
                except json.JSONDecodeError:
                    # Abaikan output non-JSON (debug print firmware)
                    if raw:
                        print(f"[GW RAW] {raw}")
            else:
                await asyncio.sleep(0.02)

        except serial.SerialException as e:
            print(f"[SERIAL] Koneksi terputus: {e}")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            await asyncio.sleep(3)

        except OSError as e:
            print(f"[SERIAL] OS error: {e}")
            ser = None
            await asyncio.sleep(3)

# ============================================================
#  Relay Watcher — Poll Firebase /relays/nodeX
#  HANYA aktif saat mode MANUAL
# ============================================================

async def relay_watcher():
    global relay_last
    print(f"[RELAY] Watcher aktif untuk Node: {NODES}")
    await asyncio.sleep(3)   # tunggu serial terhubung dulu

    # Startup: baca state relay dari Firebase
    for node in NODES:
        fb_data = await asyncio.to_thread(firebase_get, f"relays/node{node}")
        if isinstance(fb_data, dict):
            for key in ["r1", "r2"]:
                relay_last[node][key] = bool(fb_data.get(key, False))
            print(f"[RELAY] Node{node} startup state: "
                  f"r1={relay_last[node]['r1']} "
                  f"r2={relay_last[node]['r2']}")
        else:
            # Node baru — inisialisasi semua relay OFF di Firebase
            await asyncio.to_thread(firebase_put, f"relays/node{node}",
                                    {"r1": False, "r2": False})
            relay_last[node] = {"r1": False, "r2": False}
            print(f"[RELAY] Node{node} diinisialisasi (semua OFF)")

    while True:
        try:
            for node in NODES:
                # KUNCI PERBAIKAN: hanya kirim CMD relay saat mode MANUAL
                if mode_last.get(node) != "manual":
                    continue

                fb_data = await asyncio.to_thread(firebase_get, f"relays/node{node}")
                if not isinstance(fb_data, dict):
                    continue
                for key in ["r1", "r2"]:
                    new_state = bool(fb_data.get(key, False))
                    if new_state != relay_last[node][key]:
                        relay_last[node][key] = new_state
                        await send_relay_cmd(node, key, new_state)

        except Exception as e:
            print(f"[RELAY] Error: {e}")

        await asyncio.sleep(RELAY_POLL_INTERVAL)

# ============================================================
#  Mode Watcher — Poll Firebase /mode/nodeX
#  Kirim AUTORESET saat mode berubah ke "auto"
# ============================================================

async def mode_watcher():
    global mode_last
    print(f"[MODE] Watcher aktif untuk Node: {NODES}")
    await asyncio.sleep(3)   # tunggu serial terhubung dulu

    # Startup: baca mode dari Firebase dan LANGSUNG kirim SETMODE ke semua node
    # Ini memastikan node tahu mode yang benar bahkan setelah restart bridge
    for node in NODES:
        fb_mode = await asyncio.to_thread(firebase_get, f"mode/node{node}")
        if fb_mode is not None:
            mode_last[node] = str(fb_mode)
        else:
            # Inisialisasi mode auto di Firebase
            await asyncio.to_thread(firebase_put, f"mode/node{node}", "auto")
            mode_last[node] = "auto"

        print(f"[MODE] Node{node} startup: mode {mode_last[node]} → kirim SETMODE ke node")
        # Kirim mode saat ini ke node via LoRa agar state konsisten
        await send_setmode(node, mode_last[node])
        await asyncio.sleep(1)   # jeda antar node agar LoRa tidak bentrok

    while True:
        try:
            for node in NODES:
                fb_mode = await asyncio.to_thread(firebase_get, f"mode/node{node}")
                new_mode = str(fb_mode) if fb_mode else "auto"

                if new_mode != mode_last[node]:
                    old_mode = mode_last[node]
                    mode_last[node] = new_mode
                    print(f"[MODE] Node{node}: {old_mode} → {new_mode}")

                    # Kirim SETMODE ke node agar mode berubah instan
                    await send_setmode(node, new_mode)

                    if new_mode == "manual":
                        # Sync relay state dari Firebase saat masuk manual
                        fb_data = await asyncio.to_thread(
                            firebase_get, f"relays/node{node}")
                        if isinstance(fb_data, dict):
                            for key in ["r1", "r2"]:
                                relay_last[node][key] = bool(
                                    fb_data.get(key, False))
                    elif new_mode == "auto":
                        # Reset relay state tracking saat masuk auto
                        relay_last[node] = {"r1": None, "r2": None}

        except Exception as e:
            print(f"[MODE] Error: {e}")

        await asyncio.sleep(MODE_POLL_INTERVAL)

# ============================================================
#  Node Watchdog — Deteksi node offline
# ============================================================

async def node_watchdog():
    """Cek setiap 10 detik apakah node masih mengirim data."""
    await asyncio.sleep(10)
    while True:
        try:
            now = asyncio.get_event_loop().time()
            for node in NODES:
                last = node_last_rx[node]
                if last is None:
                    continue
                if node_online[node] and (now - last) > NODE_OFFLINE_TIMEOUT:
                    node_online[node] = False
                    print(f"[WATCHDOG] Node{node} OFFLINE — tidak ada data >{NODE_OFFLINE_TIMEOUT}s")
                    await asyncio.to_thread(firebase_put, f"status/node{node}", {
                        "online":    False,
                        "last_seen": datetime.now().strftime("%H:%M:%S")
                    })
        except Exception as e:
            print(f"[WATCHDOG] Error: {e}")
        await asyncio.sleep(10)

# ============================================================
#  MAIN
# ============================================================

async def main():
    print("=" * 56)
    print("  HydroIoT LoRa Bridge + Firebase")
    print(f"  Serial  : {SERIAL_PORT} @ {BAUD_RATE} baud")
    print(f"  Firebase: {FIREBASE_URL}")
    print(f"  Nodes   : {NODES}")
    print("=" * 56)

    await asyncio.gather(
        serial_reader(),
        relay_watcher(),
        mode_watcher(),
        node_watchdog(),
    )

if __name__ == "__main__":
    asyncio.run(main())
