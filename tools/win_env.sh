#!/usr/bin/env bash
# Shared setup for the WSL->Windows bridge used by flash.sh and monitor.sh.
#
# WSL2 here runs in NAT mode without usbipd-win, so the board never appears as
# /dev/ttyACM* and `pio run -t upload` cannot reach it. It does enumerate on the
# Windows side, so we stage build artifacts in the Windows temp dir and drive
# esptool/pyserial from there.
#
# Requires on the Windows side:  py -m pip install esptool
#
# Use the "UART" jack (CH343 bridge), not the "USB" jack -- the native USB ROM
# loader does not answer esptool's reset on this board.

# Default port. Override with $AMBER_PORT or a script argument.
: "${AMBER_PORT:=COM4}"

# Derive the Windows temp dir rather than hardcoding a username, so this works
# on any machine. cmd.exe must be invoked from a drvfs path or it warns about
# UNC paths and falls back to C:\Windows.
WIN_TEMP_DOS="$(cd /mnt/c && cmd.exe /c "echo %TEMP%" 2>/dev/null | tr -d '\r')"
WIN_STAGE_DOS="${WIN_TEMP_DOS}\\amberglance"
WIN_STAGE_UNIX="$(wslpath -u "$WIN_STAGE_DOS")"

mkdir -p "$WIN_STAGE_UNIX"

# Run a command through the Windows Python interpreter.
win_py() {
	(cd /mnt/c && cmd.exe /c "py $*" 2>&1 | tr -d '\r')
}
