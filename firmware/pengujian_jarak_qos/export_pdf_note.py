import os
import sys
import time
import datetime

def generate_html_pdf_report(log_filename, output_pdf_html):
    """
    Membaca log pengujian dan menghasilkan laporan HTML/PDF profesional.
    """
    if not os.path.exists(log_filename):
        print(f"[ERROR] File log '{log_filename}' tidak ditemukan!")
        return False

    records = []
    with open(log_filename, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("CSV_DATA,"):
                parts = line.strip().split(",")
                if len(parts) >= 12:
                    records.append({
                        "label": parts[1],
                        "node": parts[2],
                        "seq": parts[3],
                        "temp": parts[4],
                        "hum": parts[5],
                        "tds": parts[6],
                        "r1": parts[7],
                        "r2": parts[8],
                        "delay": parts[9],
                        "loss": parts[10],
                        "thpt": parts[11]
                    })

    now_str = datetime.datetime.now().strftime("%d %B %Y, %H:%M WIB")

    html_content = f"""<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <title>Laporan Hasil Pengujian Jarak & QoS LoRa</title>
    <style>
        body {{ font-family: 'Helvetica Neue', Arial, sans-serif; margin: 30px; color: #333; }}
        h1 {{ color: #1a365d; font-size: 22px; border-bottom: 2px solid #2b6cb0; padding-bottom: 8px; }}
        h2 {{ color: #2c5282; font-size: 16px; margin-top: 25px; }}
        .meta-table {{ width: 100%; border-collapse: collapse; margin-bottom: 20px; font-size: 13px; }}
        .meta-table td {{ padding: 6px 12px; background: #ebf8ff; border: 1px solid #bee3f8; }}
        .data-table {{ width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 12px; }}
        .data-table th {{ background: #2b6cb0; color: white; padding: 8px; text-align: center; }}
        .data-table td {{ padding: 7px; border: 1px solid #cbd5e0; text-align: center; }}
        .data-table tr:nth-child(even) {{ background: #f7fafc; }}
        .badge-ok {{ background: #c6f6d5; color: #22543d; padding: 3px 8px; borderRadius: 4px; font-weight: bold; }}
        .badge-warn {{ background: #feebc8; color: #744210; padding: 3px 8px; borderRadius: 4px; font-weight: bold; }}
        .summary-card {{ background: #edf2f7; border-left: 4px solid #3182ce; padding: 15px; margin-top: 20px; font-size: 13px; }}
    </style>
</head>
<body>
    <h1>🛰️ LAPORAN HASIL PENGUJIAN JARAK DAN QoS KOMUNIKASI LORA</h1>
    <p><strong>Sistem Monitoring & Kontrol Suhu Nutrisi Hidroponik Kangkung</strong></p>

    <table class="meta-table">
        <tr>
            <td><strong>Tanggal Pengujian:</strong> {now_str}</td>
            <td><strong>Perangkat LoRa:</strong> E32-433T20D (433 MHz, 100mW)</td>
        </tr>
        <tr>
            <td><strong>Total Paket Data Diterima:</strong> {len(records)} Paket</td>
            <td><strong>Metode Pengujian:</strong> Point-to-Point (P2P) Direct Serial</td>
        </tr>
    </table>

    <div class="summary-card">
        <strong>📋 Standar Evaluasi TIPHON (ETSI):</strong>
        <ul>
            <li><strong>Delay (Latensi):</strong> &lt; 150 ms (Sangat Baik) | &lt; 300 ms (Baik) | &lt; 450 ms (Sedang)</li>
            <li><strong>Packet Loss:</strong> &lt; 3% (Sangat Baik) | &lt; 15% (Baik) | &lt; 25% (Sedang)</li>
        </ul>
    </div>

    <h2>📊 Tabel Record Pengujian Jarak & Data Sensor</h2>
    <table class="data-table">
        <thead>
            <tr>
                <th>No</th>
                <th>Kondisi / Jarak</th>
                <th>Node</th>
                <th>Seq #</th>
                <th>Suhu (°C)</th>
                <th>Kelembapan (%)</th>
                <th>TDS (ppm)</th>
                <th>Delay (ms)</th>
                <th>Packet Loss (%)</th>
                <th>Throughput (bps)</th>
                <th>Status TIPHON</th>
            </tr>
        </thead>
        <tbody>
"""

    for i, r in enumerate(records, 1):
        loss_val = float(r['loss'])
        status_badge = '<span class="badge-ok">Sangat Baik</span>' if loss_val < 3.0 else '<span class="badge-warn">Baik</span>'
        html_content += f"""
            <tr>
                <td>{i}</td>
                <td><strong>{r['label']}</strong></td>
                <td>Node {r['node']}</td>
                <td>#{r['seq']}</td>
                <td>{r['temp']}</td>
                <td>{r['hum']}</td>
                <td>{r['tds']}</td>
                <td>{r['delay']} ms</td>
                <td>{r['loss']}%</td>
                <td>{r['thpt']} bps</td>
                <td>{status_badge}</td>
            </tr>
"""

    html_content += """
        </tbody>
    </table>

    <div style="margin-top: 40px; font-size: 11px; color: #718096; text-align: center;">
        Dicetak otomatis oleh Tool Pengujian Jarak & QoS Hidroponik IoT — Universitas Komputer Indonesia (UNIKOM)
    </div>
</body>
</html>
"""

    with open(output_pdf_html, "w", encoding="utf-8") as out:
        out.write(html_content)

    print(f"\n[SUKSES] Laporan HTML/PDF Pengujian telah dibuat: {output_pdf_html}")
    print("[PETUNJUK] Buka file HTML ini di browser (Chrome/Edge), lalu tekan Ctrl + P -> 'Save as PDF' untuk menyimpan sebagai PDF!")
    return True

if __name__ == "__main__":
    log_file = "catatan_log_pengujian.txt"
    if len(sys.argv) > 1:
        log_file = sys.argv[1]
    
    out_html = "laporan_pengujian_jarak_qos.html"
    generate_html_pdf_report(log_file, out_html)
