@echo off
echo ============================================
echo   HydroIoT LoRa Bridge - Starting...
echo   Port: COM3  ^|  Baud: 115200
echo   Firebase: hidroponik-server
echo ============================================
echo.
echo [INFO] Pastikan Gateway ESP32 sudah terhubung ke COM3
echo [INFO] Tekan Ctrl+C untuk berhenti
echo.
C:\Users\Anggito\AppData\Local\Python\bin\python.exe "%~dp0lora_bridge.py"
pause
