@echo off
echo Uploading LittleFS image using ESP-IDF esptool...
cd /d "C:\Users\rober\esp\v5.4\esp-idf\components\esptool_py\esptool"
python esptool.py --port COM3 write_flash 0x580000 "%~dp0littlefs_image.bin"
echo Upload complete!
pause
