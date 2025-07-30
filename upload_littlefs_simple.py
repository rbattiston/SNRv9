#!/usr/bin/env python3
import os
import sys

# Add PlatformIO's Python packages to path
pio_python = os.path.expanduser("~/.platformio/python3/python.exe")
pio_packages = os.path.expanduser("~/.platformio/packages")

# Use PlatformIO's Python to run esptool
esptool_dir = os.path.join(pio_packages, "tool-esptoolpy")
esptool_script = os.path.join(esptool_dir, "esptool.py")

# Change to esptool directory and run
os.chdir(esptool_dir)
cmd = f'"{pio_python}" esptool.py --port COM3 write_flash 0x580000 "{os.path.abspath("../../littlefs_image.bin")}"'
print(f"Executing: {cmd}")
os.system(cmd)
