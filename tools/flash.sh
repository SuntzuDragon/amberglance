#!/usr/bin/env bash
# Build in WSL, flash from Windows. See tools/win_env.sh for why.
#
# Usage: tools/flash.sh [COM_PORT]
set -euo pipefail

cd "$(dirname "$0")/.."
source tools/win_env.sh

PORT="${1:-$AMBER_PORT}"
BUILD_DIR=".pio/build/esp32-s3-devkitc-1"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

.venv/bin/pio run

cp "$BUILD_DIR/bootloader.bin" "$BUILD_DIR/partitions.bin" \
   "$BUILD_DIR/firmware.bin" "$BOOT_APP0" "$WIN_STAGE_UNIX/"

echo "flashing $PORT ..."
# Offsets and flash params match what `pio run -t upload` would use.
win_py "-m esptool --chip esp32s3 --port $PORT --baud 921600 \
  write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0000 $WIN_STAGE_DOS\\bootloader.bin \
  0x8000 $WIN_STAGE_DOS\\partitions.bin \
  0xe000 $WIN_STAGE_DOS\\boot_app0.bin \
  0x10000 $WIN_STAGE_DOS\\firmware.bin"
