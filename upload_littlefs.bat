@echo off
cd /d "%USERPROFILE%\.platformio\packages\tool-esptoolpy"
"%USERPROFILE%\.platformio\python3\python.exe" esptool.py --port COM3 write_flash 0x580000 "%~dp0littlefs_image.bin"
pause
