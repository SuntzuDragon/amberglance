#!/usr/bin/env bash
# Capture serial output from the Windows side. See tools/win_env.sh for why.
# Pulses RTS on open to reset the board, so you always catch the boot log.
#
# Usage: tools/monitor.sh [SECONDS] [COM_PORT]
set -euo pipefail

cd "$(dirname "$0")/.."
source tools/win_env.sh

SECS="${1:-10}"
PORT="${2:-$AMBER_PORT}"

cp tools/capture.py "$WIN_STAGE_UNIX/"
win_py "$WIN_STAGE_DOS\\capture.py $PORT $SECS"
