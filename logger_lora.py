#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════╗
║        LOGGER SERIAL LoRa → CSV                              ║
║  Membaca data dari Serial Monitor dan menyimpan ke CSV       ║
║                                                              ║
║  Cara pakai:                                                 ║
║    python logger_lora.py                          (COM3 default)
║    python logger_lora.py --port COM5              (ganti port)
║    python logger_lora.py --port COM5 --output data.csv       ║
║                                                              ║
║  Tekan Ctrl+C untuk berhenti → Excel otomatis dibuat        ║
╚══════════════════════════════════════════════════════════════╝
"""

import argparse
import csv
import serial
import serial.tools.list_ports
import sys
import os
from datetime import datetime


# ─── Daftar port tersedia ──────────────────────────────────────
def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("  (Tidak ada port serial yang terdeteksi)")
    for p in ports:
        print(f"  {p.device:10s} — {p.description}")


# ─── Main ──────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Logger Serial LoRa ke CSV + Excel"
    )
    parser.add_argument("--port",   default="COM3",   help="Port Serial (default: COM3)")
    parser.add_argument("--baud",   default=115200,   type=int, help="Baud rate (default: 115200)")
    parser.add_argument("--output", default=None,     help="Nama file output CSV")
    args = parser.parse_args()

    # Nama file otomatis pakai timestamp
    if args.output is None:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = f"log_lora_{ts}.csv"

    excel_output = args.output.replace(".csv", ".xlsx")

    print("=" * 62)
    print("  LOGGER SERIAL LoRa")
    print("=" * 62)
    print(f"  Port   : {args.port}")
    print(f"  Baud   : {args.baud}")
    print(f"  Output : {args.output}")
    print()
    print("  Port yang tersedia:")
    list_ports()
    print()
    print("  Tekan Ctrl+C untuk berhenti dan generate Excel")
    print("=" * 62)

    # Buka koneksi serial
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        print(f"\n  [OK] Terhubung ke {args.port}\n")
    except serial.SerialException as e:
        print(f"\n  [ERROR] Tidak bisa buka {args.port}: {e}")
        print("  Coba jalankan: python logger_lora.py --port COMx")
        sys.exit(1)

    rows = []
    sesi = 1
    packet_count = 0

    print(f"  {'No':>4} | {'Sesi':>4} | {'Seq':>5} | {'Diterima':>8} | {'Hilang':>6} | {'Loss':>6} | {'Delay':>8} | Waktu")
    print("  " + "-" * 68)

    try:
        with open(args.output, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                "No", "Sesi", "Seq", "Total_Diterima",
                "Total_Hilang", "Loss_Rate_%", "Delay_ms", "Waktu"
            ])

            last_seq = -1

            while True:
                try:
                    raw = ser.readline()
                    line = raw.decode("utf-8", errors="ignore").strip()

                    # Deteksi node restart dari notifikasi firmware
                    if "NODE RESTART" in line:
                        sesi += 1
                        last_seq = -1
                        print(f"\n  *** SESI BARU #{sesi} — Node Restart Terdeteksi ***\n")

                    # Hanya proses baris data CSV
                    if not line.startswith("DATA:"):
                        continue

                    parts = line[5:].split(",")
                    if len(parts) < 6:
                        continue

                    no_rx     = parts[0]
                    seq       = int(parts[1])
                    total_rx  = parts[2]
                    total_lost= parts[3]
                    loss_rate = parts[4]
                    delay_ms  = parts[5]
                    waktu     = datetime.now().strftime("%H:%M:%S")

                    packet_count += 1
                    row = [packet_count, sesi, seq, total_rx, total_lost, loss_rate, delay_ms, waktu]
                    writer.writerow(row)
                    f.flush()
                    rows.append(row)

                    # Tampilan konsol
                    loss_f = float(loss_rate)
                    delay_i = int(delay_ms)
                    loss_icon = "OK" if loss_f == 0 else "!!"
                    delay_icon = "OK" if delay_i <= 2100 else ("!?" if delay_i <= 2500 else "!!")

                    print(f"  {packet_count:4d} | {sesi:4d} | {seq:5d} | "
                          f"{total_rx:8s} | {total_lost:6s} | "
                          f"{loss_rate:5s}% {loss_icon} | "
                          f"{delay_ms:5s}ms {delay_icon} | {waktu}")

                    last_seq = seq

                except UnicodeDecodeError:
                    pass

    except KeyboardInterrupt:
        print("\n\n  Logger dihentikan.")
        ser.close()

    # ─── Generate Excel ──────────────────────────────────────
    if rows:
        print(f"  [OK] {len(rows)} paket tersimpan di: {args.output}")
        print(f"  Membuat Excel...")
        try:
            from buat_excel import generate_excel
            generate_excel(args.output, excel_output)
            print(f"  [OK] Excel berhasil: {excel_output}")
        except ImportError:
            print(f"  ! File buat_excel.py tidak ditemukan.")
            print(f"    Jalankan manual: python buat_excel.py --input {args.output}")
        except Exception as e:
            print(f"  [ERROR] Gagal buat Excel: {e}")
    else:
        print("  Tidak ada data yang tersimpan.")


if __name__ == "__main__":
    main()
