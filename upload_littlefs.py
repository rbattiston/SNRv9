#!/usr/bin/env python3
import subprocess
import sys
import os

# Get the PlatformIO tools path
pio_packages = os.path.expanduser("~/.platformio/packages")
esptool_path = os.path.join(pio_packages, "tool-esptoolpy", "esptool.py")

if not os.path.exists(esptool_path):
    print(f"Error: esptool.py not found at {esptool_path}")
    sys.exit(1)

# Upload the LittleFS image to the storage partition
cmd = [
    sys.executable,
    esptool_path,
    "--port", "COM3",
    "write_flash",
    "0x580000",  # Storage partition offset
    "littlefs_image.bin"
]

print(f"Executing: {' '.join(cmd)}")
result = subprocess.run(cmd, capture_output=True, text=True)

print("STDOUT:")
print(result.stdout)
if result.stderr:
    print("STDERR:")
    print(result.stderr)

sys.exit(result.returncode)
