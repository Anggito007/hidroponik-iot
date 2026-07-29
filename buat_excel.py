#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════╗
║        BUAT EXCEL + GRAFIK dari CSV LoRa                     ║
║                                                              ║
║  Cara pakai:                                                 ║
║    python buat_excel.py                   (pakai CSV terbaru)║
║    python buat_excel.py --input data.csv  (pilih file)       ║
║    python buat_excel.py --input data.csv --output hasil.xlsx ║
╚══════════════════════════════════════════════════════════════╝
"""

import argparse
import csv
import os
import sys
import glob
from datetime import datetime


# ─── Cek dan install library ──────────────────────────────────
def check_install(package, import_name=None):
    import_name = import_name or package
    try:
        __import__(import_name)
    except ImportError:
        print(f"  Menginstall {package}...")
        import subprocess
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])


check_install("openpyxl")

import openpyxl
from openpyxl.styles import (
    Font, PatternFill, Alignment, Border, Side,
    GradientFill
)
from openpyxl.chart import LineChart, BarChart, Reference
from openpyxl.chart.series import SeriesLabel
from openpyxl.utils import get_column_letter


# ─── Warna tema ───────────────────────────────────────────────
BIRU_TUA  = "1F3864"
BIRU_MUDA = "2E75B6"
HIJAU     = "70AD47"
MERAH     = "FF0000"
KUNING    = "FFC000"
ABU_TERANG= "D6E4F7"
ABU_HEADER= "BDD7EE"
PUTIH     = "FFFFFF"


def border_thin():
    s = Side(style="thin", color="B0B0B0")
    return Border(left=s, right=s, top=s, bottom=s)


# ─── Baca CSV ─────────────────────────────────────────────────
def read_csv(filepath):
    rows = []
    with open(filepath, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            try:
                rows.append({
                    "no":           int(r["No"]),
                    "sesi":         int(r.get("Sesi", 1)),
                    "seq":          int(r["Seq"]),
                    "total_rx":     int(r["Total_Diterima"]),
                    "total_lost":   int(r["Total_Hilang"]),
                    "loss_rate":    float(r["Loss_Rate_%"]),
                    "delay_ms":     int(r["Delay_ms"]),
                    "waktu":        r.get("Waktu", ""),
                })
            except (ValueError, KeyError):
                pass
    return rows


# ─── Hitung statistik ─────────────────────────────────────────
def calc_stats(rows):
    if not rows:
        return {}
    delays   = [r["delay_ms"] for r in rows if r["delay_ms"] > 0]
    losses   = [r["loss_rate"] for r in rows]
    last     = rows[-1]
    return {
        "total_paket":    last["total_rx"] + last["total_lost"],
        "total_diterima": last["total_rx"],
        "total_hilang":   last["total_lost"],
        "loss_akhir":     last["loss_rate"],
        "delay_avg":      round(sum(delays) / len(delays), 1) if delays else 0,
        "delay_min":      min(delays) if delays else 0,
        "delay_max":      max(delays) if delays else 0,
        "delay_jitter":   max(delays) - min(delays) if delays else 0,
        "sesi_total":     max(r["sesi"] for r in rows),
    }


# ─── Sheet: Ringkasan ─────────────────────────────────────────
def write_sheet_ringkasan(wb, rows, stats, csv_file):
    ws = wb.active
    ws.title = "Ringkasan"
    ws.column_dimensions["A"].width = 28
    ws.column_dimensions["B"].width = 22
    ws.column_dimensions["C"].width = 16

    # ── Judul ──
    ws.merge_cells("A1:C1")
    c = ws["A1"]
    c.value = "LAPORAN PENGUJIAN LoRa E32"
    c.font  = Font(name="Segoe UI", size=16, bold=True, color=PUTIH)
    c.fill  = PatternFill("solid", fgColor=BIRU_TUA)
    c.alignment = Alignment(horizontal="center", vertical="center")
    ws.row_dimensions[1].height = 36

    ws.merge_cells("A2:C2")
    c = ws["A2"]
    c.value = f"Dibuat: {datetime.now().strftime('%d %B %Y, %H:%M:%S')}  |  File: {os.path.basename(csv_file)}"
    c.font  = Font(name="Segoe UI", size=10, italic=True, color="555555")
    c.fill  = PatternFill("solid", fgColor=ABU_HEADER)
    c.alignment = Alignment(horizontal="center")

    ws.append([])

    # ── Tabel statistik ──
    def tulis_baris(label, nilai, satuan="", warna_nilai=None):
        row = ws.max_row + 1
        ws.cell(row, 1, label).font        = Font(name="Segoe UI", size=11, bold=True, color=BIRU_TUA)
        ws.cell(row, 1).fill               = PatternFill("solid", fgColor=ABU_TERANG)
        ws.cell(row, 1).border             = border_thin()
        ws.cell(row, 1).alignment          = Alignment(horizontal="left", indent=1)

        ws.cell(row, 2, nilai).font        = Font(name="Segoe UI", size=11, bold=True,
                                                   color=warna_nilai or BIRU_TUA)
        ws.cell(row, 2).border             = border_thin()
        ws.cell(row, 2).alignment          = Alignment(horizontal="center")

        ws.cell(row, 3, satuan).font       = Font(name="Segoe UI", size=10, color="777777")
        ws.cell(row, 3).border             = border_thin()
        ws.cell(row, 3).alignment          = Alignment(horizontal="left")

    # Header sub-tabel
    for col, txt in enumerate(["Parameter", "Nilai", "Satuan"], 1):
        c = ws.cell(ws.max_row + 1, col, txt)
        c.font      = Font(name="Segoe UI", size=11, bold=True, color=PUTIH)
        c.fill      = PatternFill("solid", fgColor=BIRU_MUDA)
        c.border    = border_thin()
        c.alignment = Alignment(horizontal="center")
    ws.row_dimensions[ws.max_row].height = 22

    tulis_baris("Total Paket Dikirim (est.)",   stats["total_paket"],    "paket")
    tulis_baris("Total Paket Diterima",         stats["total_diterima"], "paket", HIJAU)

    loss_color = MERAH if stats["loss_akhir"] > 10 else (KUNING if stats["loss_akhir"] > 0 else HIJAU)
    tulis_baris("Total Paket Hilang",           stats["total_hilang"],   "paket", loss_color)
    tulis_baris("Packet Loss",                  f"{stats['loss_akhir']:.1f}%", "", loss_color)

    ws.append([])

    delay_color = MERAH if stats["delay_max"] > 2500 else (KUNING if stats["delay_max"] > 2100 else HIJAU)
    tulis_baris("Delay Rata-rata",              f"{stats['delay_avg']} ms",   "", BIRU_TUA)
    tulis_baris("Delay Minimum",                f"{stats['delay_min']} ms",   "", HIJAU)
    tulis_baris("Delay Maximum",                f"{stats['delay_max']} ms",   "", delay_color)
    tulis_baris("Jitter (Max - Min)",           f"{stats['delay_jitter']} ms","", BIRU_TUA)

    ws.append([])
    tulis_baris("Jumlah Sesi (Node Restart)",   stats["sesi_total"],     "sesi")
    tulis_baris("Total Data Tercatat",          len(rows),               "baris")

    # Warna semua row height
    for row in ws.iter_rows(min_row=4, max_row=ws.max_row):
        ws.row_dimensions[row[0].row].height = 20


# ─── Sheet: Data ──────────────────────────────────────────────
def write_sheet_data(wb, rows):
    ws = wb.create_sheet("Data")
    ws.freeze_panes = "A2"

    headers = ["No", "Sesi", "Seq", "Total Diterima", "Total Hilang",
               "Loss Rate (%)", "Delay (ms)", "Waktu"]
    col_widths = [6, 6, 7, 16, 14, 14, 12, 10]

    for i, (h, w) in enumerate(zip(headers, col_widths), 1):
        ws.column_dimensions[get_column_letter(i)].width = w
        c = ws.cell(1, i, h)
        c.font      = Font(name="Segoe UI", size=10, bold=True, color=PUTIH)
        c.fill      = PatternFill("solid", fgColor=BIRU_TUA)
        c.border    = border_thin()
        c.alignment = Alignment(horizontal="center")

    ws.row_dimensions[1].height = 22

    for r in rows:
        row_num = r["no"] + 1
        values = [r["no"], r["sesi"], r["seq"], r["total_rx"],
                  r["total_lost"], r["loss_rate"], r["delay_ms"], r["waktu"]]

        for col, val in enumerate(values, 1):
            c = ws.cell(row_num, col, val)
            c.font      = Font(name="Segoe UI", size=10)
            c.border    = border_thin()
            c.alignment = Alignment(horizontal="center")

        # Warna baris berdasarkan kondisi
        loss = r["loss_rate"]
        delay = r["delay_ms"]
        fill_color = None
        if loss > 10:
            fill_color = "FFCCCC"   # merah muda — loss tinggi
        elif delay > 2500:
            fill_color = "FFF2CC"   # kuning — delay sangat tinggi
        elif row_num % 2 == 0:
            fill_color = "EBF3FB"   # biru muda — zebra stripe

        if fill_color:
            for col in range(1, len(headers) + 1):
                ws.cell(row_num, col).fill = PatternFill("solid", fgColor=fill_color)
                ws.cell(row_num, col).border = border_thin()

    ws.row_dimensions[ws.max_row].height = 18


# ─── Sheet: Grafik ────────────────────────────────────────────
def write_sheet_grafik(wb, rows):
    ws = wb.create_sheet("Grafik")

    # ── Tulis data tersembunyi untuk chart ──
    # Kolom A = No, B = Delay, C = Loss Rate
    ws.cell(1, 1, "No").font         = Font(bold=True)
    ws.cell(1, 2, "Delay (ms)").font = Font(bold=True)
    ws.cell(1, 3, "Loss (%)").font   = Font(bold=True)
    ws.cell(1, 4, "Target Delay").font = Font(bold=True)

    for i, r in enumerate(rows, 2):
        ws.cell(i, 1, r["no"])
        ws.cell(i, 2, r["delay_ms"])
        ws.cell(i, 3, r["loss_rate"])
        ws.cell(i, 4, 2000)   # garis referensi target delay

    n = len(rows) + 1   # baris terakhir data

    # ═══════════════════════════════════════════
    # GRAFIK 1: Delay (ms) vs No Paket
    # ═══════════════════════════════════════════
    chart1 = LineChart()
    chart1.title     = "Delay Antar Paket (ms)"
    chart1.style     = 10
    chart1.y_axis.title = "Delay (ms)"
    chart1.x_axis.title = "No Paket"
    chart1.height    = 12
    chart1.width     = 22
    chart1.y_axis.scaling.min = 1500
    chart1.y_axis.scaling.max = 3000

    # Data delay
    data_delay = Reference(ws, min_col=2, min_row=1, max_row=n)
    chart1.add_data(data_delay, titles_from_data=True)
    chart1.series[0].graphicalProperties.line.solidFill  = "2E75B6"
    chart1.series[0].graphicalProperties.line.width      = 20000
    chart1.series[0].smooth = True

    # Garis referensi 2000ms
    data_target = Reference(ws, min_col=4, min_row=1, max_row=n)
    chart1.add_data(data_target, titles_from_data=True)
    chart1.series[1].graphicalProperties.line.solidFill  = "FF0000"
    chart1.series[1].graphicalProperties.line.dashDot    = "dash"
    chart1.series[1].graphicalProperties.line.width      = 15000
    chart1.series[1].title = SeriesLabel(v="Target (2000ms)")

    # X axis = No Paket
    cats = Reference(ws, min_col=1, min_row=2, max_row=n)
    chart1.set_categories(cats)

    ws.add_chart(chart1, "F1")

    # ═══════════════════════════════════════════
    # GRAFIK 2: Packet Loss (%) vs No Paket
    # ═══════════════════════════════════════════
    chart2 = LineChart()
    chart2.title          = "Packet Loss (%) Kumulatif"
    chart2.style          = 10
    chart2.y_axis.title   = "Loss Rate (%)"
    chart2.x_axis.title   = "No Paket"
    chart2.height         = 12
    chart2.width          = 22

    data_loss = Reference(ws, min_col=3, min_row=1, max_row=n)
    chart2.add_data(data_loss, titles_from_data=True)
    chart2.series[0].graphicalProperties.line.solidFill = "FF0000"
    chart2.series[0].graphicalProperties.line.width     = 20000
    chart2.series[0].smooth = True
    chart2.set_categories(cats)

    ws.add_chart(chart2, "F26")

    # ═══════════════════════════════════════════
    # GRAFIK 3: Bar — Diterima vs Hilang
    # ═══════════════════════════════════════════
    # Tulis ringkasan untuk bar chart di kolom F-G
    last = rows[-1] if rows else None
    if last:
        ws.cell(1, 6, "Kategori")
        ws.cell(1, 7, "Jumlah")
        ws.cell(2, 6, "Diterima")
        ws.cell(2, 7, last["total_rx"])
        ws.cell(3, 6, "Hilang")
        ws.cell(3, 7, last["total_lost"])

        chart3 = BarChart()
        chart3.title          = "Total Paket: Diterima vs Hilang"
        chart3.style          = 10
        chart3.y_axis.title   = "Jumlah Paket"
        chart3.x_axis.title   = ""
        chart3.height         = 12
        chart3.width          = 14
        chart3.type           = "col"

        data_bar = Reference(ws, min_col=7, min_row=1, max_row=3)
        chart3.add_data(data_bar, titles_from_data=True)
        cats3 = Reference(ws, min_col=6, min_row=2, max_row=3)
        chart3.set_categories(cats3)
        chart3.series[0].graphicalProperties.solidFill   = "2E75B6"
        chart3.series[0].dLbls                           = None

        ws.add_chart(chart3, "F51")

    # Sembunyikan kolom data mentah (opsional)
    ws.column_dimensions["A"].width = 8
    ws.column_dimensions["B"].width = 10
    ws.column_dimensions["C"].width = 10
    ws.column_dimensions["D"].width = 12


# ─── Main generate ────────────────────────────────────────────
def generate_excel(csv_file, excel_file):
    print(f"  Membaca: {csv_file}")
    rows = read_csv(csv_file)
    if not rows:
        print("  [ERROR] Tidak ada data di file CSV!")
        return

    print(f"  {len(rows)} baris data ditemukan")
    stats = calc_stats(rows)

    wb = openpyxl.Workbook()

    write_sheet_ringkasan(wb, rows, stats, csv_file)
    write_sheet_data(wb, rows)
    write_sheet_grafik(wb, rows)

    # Set sheet aktif ke Ringkasan
    wb.active = wb["Ringkasan"]

    wb.save(excel_file)
    print(f"  ✓ Tersimpan: {excel_file}")

    # Buka otomatis di Excel
    try:
        os.startfile(excel_file)
        print("  ✓ Excel dibuka otomatis")
    except Exception:
        pass


# ─── Entry point ──────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Buat Excel + Grafik dari CSV LoRa")
    parser.add_argument("--input",  default=None, help="File CSV input")
    parser.add_argument("--output", default=None, help="File Excel output (.xlsx)")
    args = parser.parse_args()

    # Auto-cari CSV terbaru jika tidak ditentukan
    if args.input is None:
        csv_files = sorted(glob.glob("log_lora_*.csv"), reverse=True)
        if csv_files:
            args.input = csv_files[0]
            print(f"  Auto-pilih file: {args.input}")
        else:
            print("  [ERROR] Tidak ada file CSV ditemukan.")
            print("  Jalankan: python buat_excel.py --input namafile.csv")
            sys.exit(1)

    if args.output is None:
        args.output = args.input.replace(".csv", ".xlsx")

    print("=" * 50)
    print("  BUAT EXCEL + GRAFIK LoRa")
    print("=" * 50)
    generate_excel(args.input, args.output)
    print("=" * 50)
    print("  Selesai!")
