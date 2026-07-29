@echo off
echo ============================================
echo   INSTALL LIBRARY PYTHON untuk HydroIoT
echo ============================================
echo.
echo Menginstall pyserial ...
pip install pyserial
echo.
echo Menginstall requests ...
pip install requests
echo.
echo Menginstall openpyxl ...
pip install openpyxl
echo.
echo ============================================
echo   Semua library berhasil diinstall!
echo ============================================
echo.
echo Cara pakai:
echo   1. Jalankan lora_bridge: python server/lora_bridge.py
echo   2. Buka dashboard: http://localhost:8080
echo   3. Logger LoRa: python logger_lora.py --port COM3
echo.
pause
